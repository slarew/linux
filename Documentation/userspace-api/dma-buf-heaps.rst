.. SPDX-License-Identifier: GPL-2.0

==============================
Allocating dma-buf using heaps
==============================

Dma-buf Heaps are a way for userspace to allocate dma-buf objects. They are
typically used to allocate buffers from a specific allocation pool, or to share
buffers across frameworks.

Heaps
=====

A heap represents a specific allocator. The Linux kernel currently supports the
following heaps:

 - The ``system`` heap allocates virtually contiguous, cacheable, buffers.

 - The ``cma`` heap allocates physically contiguous, cacheable,
   buffers. Only present if a CMA region is present. Such a region is
   usually created either through the kernel commandline through the
   ``cma`` parameter, a memory region Device-Tree node with the
   ``linux,cma-default`` property set, or through the ``CMA_SIZE_MBYTES`` or
   ``CMA_SIZE_PERCENTAGE`` Kconfig options. The heap's name in devtmpfs is
   ``default_cma_region``. For backwards compatibility, when the
   ``DMABUF_HEAPS_CMA_LEGACY`` Kconfig option is set, a duplicate node is
   created following legacy naming conventions; the legacy name might be
   ``reserved``, ``linux,cma``, or ``default-pool``.
