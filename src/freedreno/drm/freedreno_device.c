/*
 * Copyright © 2012-2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xf86drm.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/os_file.h"
#include "util/u_process.h"

#include "freedreno_rd_output.h"

#include "freedreno_drmif.h"
#include "freedreno_drm_perfetto.h"
#include "freedreno_priv.h"

struct fd_device *msm_device_new(int fd, drmVersionPtr version);
#ifdef HAVE_FREEDRENO_VIRTIO
struct fd_device *virtio_device_new(int fd, drmVersionPtr version);
#endif

#ifdef HAVE_FREEDRENO_KGSL
struct fd_device *kgsl_device_new(int fd);
#endif

uint64_t os_page_size = 4096;

#ifdef HAVE_LIBDRM
static drmVersionPtr
fd_get_device_version(int fd, const char* fd_ver)
{
    drmVersionPtr ver = NULL;
    ver = drmGetVersion(fd);

    if (!ver) {
        printf("[ FD Device ] Cannot get version: %s, Trying to impersonate ...\n", strerror(errno));
        if (fd_ver) goto fakeVersion;
        printf("[ FD Device ] Failed...\n");
        goto fail;
    }

    return ver;

fakeVersion:
    drmVersionPtr fake = calloc(1, sizeof(struct drm_version));

    if (!strcmp(fd_ver, "msm")) {
        fake->version_major = 1;
        fake->version_minor = 0;
        fake->version_patchlevel = 0;

        fake->name = strdup("msm");
        fake->name_len = strlen(fake->name);
        fake->desc = strdup("Qualcomm MSM DRM driver");
        fake->desc_len = strlen(fake->desc);
        fake->date = strdup("20250625");
        fake->date_len = strlen(fake->date);
    } else {
        goto fail;
    }

    return fake;

fail:
    return NULL;
}
#endif

struct fd_device *
fd_device_new(int fd)
{
   const char* fd_ver = getenv("MESA_LOADER_DRIVER_OVERRIDE");
   struct fd_device *dev = NULL;
   drmVersionPtr version = NULL;
   bool use_heap = false;
   bool support_use_heap = true;

   os_get_page_size(&os_page_size);

#ifdef HAVE_LIBDRM
   /* figure out if we are kgsl or msm drm driver: */
   if (fd_ver == NULL) {
      printf("[ FD Device ] Cannot get config, try get Version...\n");
      version = drmGetVersion(fd);
   } else if (!strcmp(fd_ver, "msm")) {
      version = fd_get_device_version(fd, fd_ver);
   } else if (!strcmp(fd_ver, "virtio_gpu")) {
      printf("[ FD Device ] VirtioGPU is not supported ...\n");
      return NULL;
   }
   if (!version) {
      printf("[ FD Device ] Cannot get version: %s\n", strerror(errno));
      return NULL;
   }
#endif

#ifdef HAVE_FREEDRENO_VIRTIO
   if (debug_get_bool_option("FD_FORCE_VTEST", false)) {
      DEBUG_MSG("virtio_gpu vtest device");
      dev = virtio_device_new(-1, version);
   } else
#endif
   if (version && !strcmp(version->name, "msm")) {
      printf("[ FD Device ] msm DRM device\n");
      if (version->version_major != 1) {
         ERROR_MSG("unsupported version: %u.%u.%u", version->version_major,
                   version->version_minor, version->version_patchlevel);
         goto out;
      }

      dev = msm_device_new(fd, version);
#ifdef HAVE_FREEDRENO_VIRTIO
   } else if (version && !strcmp(version->name, "virtio_gpu")) {
      DEBUG_MSG("virtio_gpu DRM device");
      dev = virtio_device_new(fd, version);
      /* Only devices that support a hypervisor are a6xx+, so avoid the
       * extra guest<->host round trips associated with pipe creation:
       */
      use_heap = true;
#endif
#if HAVE_FREEDRENO_KGSL
   } else {
      printf("[ FD Device ] kgsl DRM device\n");
      dev = kgsl_device_new(fd);
      support_use_heap = false;
      if (dev)
         goto out;
#endif
   }

   if (!dev) {
      printf("unsupported device: %s\n", fd_ver);
      goto out;
   }

out:
   drmFreeVersion(version);

   if (!dev)
      return NULL;

   fd_drm_perfetto_init();

   fd_rd_dump_env_init();
   fd_rd_output_init(&dev->rd, util_get_process_name());

   p_atomic_set(&dev->refcnt, 1);
   dev->fd = fd;
   dev->handle_table =
      _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);
   dev->name_table =
      _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);
   fd_bo_cache_init(&dev->bo_cache, false, "bo");
   fd_bo_cache_init(&dev->ring_cache, true, "ring");

   list_inithead(&dev->deferred_submits);
   simple_mtx_init(&dev->submit_lock, mtx_plain);
   simple_mtx_init(&dev->suballoc_lock, mtx_plain);

   if (support_use_heap && !use_heap) {
      struct fd_pipe *pipe = fd_pipe_new(dev, FD_PIPE_3D);

      if (!pipe)
         goto fail;

      /* Userspace fences don't appear to be reliable enough (missing some
       * cache flushes?) on older gens, so limit sub-alloc heaps to a6xx+
       * for now:
       */
      use_heap = fd_dev_gen(&pipe->dev_id) >= 6;

      fd_pipe_del(pipe);
   }

   if (use_heap) {
      dev->ring_heap = fd_bo_heap_new(dev, RING_FLAGS);
      dev->default_heap = fd_bo_heap_new(dev, 0);
   }

   return dev;

fail:
   fd_device_del(dev);
   return NULL;
}

/* like fd_device_new() but creates it's own private dup() of the fd
 * which is close()d when the device is finalized.
 */
struct fd_device *
fd_device_new_dup(int fd)
{
   int dup_fd = os_dupfd_cloexec(fd);
   struct fd_device *dev = fd_device_new(dup_fd);
   if (dev)
      dev->closefd = 1;
   else
      close(dup_fd);
   return dev;
}

/* Convenience helper to open the drm device and return new fd_device:
 */
struct fd_device *
fd_device_open(void)
{
   int fd;

   fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
#ifdef HAVE_FREEDRENO_VIRTIO
   if (fd < 0)
      fd = drmOpenWithType("virtio_gpu", NULL, DRM_NODE_RENDER);
#endif
   if (fd < 0)
      return NULL;

   return fd_device_new(fd);
}

struct fd_device *
fd_device_ref(struct fd_device *dev)
{
   ref(&dev->refcnt);
   return dev;
}

void
fd_device_purge(struct fd_device *dev)
{
   fd_bo_cache_cleanup(&dev->bo_cache, 0);
   fd_bo_cache_cleanup(&dev->ring_cache, 0);
}

void
fd_device_del(struct fd_device *dev)
{
   if (!unref(&dev->refcnt))
      return;

   fd_rd_output_fini(&dev->rd);

   assert(list_is_empty(&dev->deferred_submits));
   assert(!dev->deferred_submits_fence);

   if (dev->suballoc_bo)
      fd_bo_del(dev->suballoc_bo);

   if (dev->ring_heap)
      fd_bo_heap_destroy(dev->ring_heap);

   if (dev->default_heap)
      fd_bo_heap_destroy(dev->default_heap);

   fd_bo_cache_cleanup(&dev->bo_cache, 0);
   fd_bo_cache_cleanup(&dev->ring_cache, 0);

   /* Needs to be after bo cache cleanup in case backend has a
    * util_vma_heap that it destroys:
    */
   dev->funcs->destroy(dev);

   _mesa_hash_table_destroy(dev->handle_table, NULL);
   _mesa_hash_table_destroy(dev->name_table, NULL);

   if (fd_device_threaded_submit(dev))
      util_queue_destroy(&dev->submit_queue);

   if (dev->closefd)
      close(dev->fd);

   free(dev);
}

int
fd_device_fd(struct fd_device *dev)
{
   return dev->fd;
}

enum fd_version
fd_device_version(struct fd_device *dev)
{
   return dev->version;
}

DEBUG_GET_ONCE_BOOL_OPTION(libgl, "LIBGL_DEBUG", false)

bool
fd_dbg(void)
{
   return debug_get_option_libgl();
}

uint32_t
fd_get_features(struct fd_device *dev)
{
    return dev->features;
}

bool
fd_has_syncobj(struct fd_device *dev)
{
   uint64_t value;
   if (drmGetCap(dev->fd, DRM_CAP_SYNCOBJ, &value))
      return false;
   return value && dev->version >= FD_VERSION_FENCE_FD;
}
