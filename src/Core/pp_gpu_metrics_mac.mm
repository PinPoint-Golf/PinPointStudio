/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// macOS Metal backend for pp_gpu_metrics.  Compiled (ObjC++, ARC) only on Apple
// and called from pp_gpu_metrics.cpp's #if defined(__APPLE__) branch.
//
// MTLDevice gives a per-process view: currentAllocatedSize is the size of all
// resources this process has allocated on the device; recommendedMaxWorkingSetSize
// is the budget we treat as "total".  On Apple Silicon the GPU shares system RAM
// (hasUnifiedMemory == YES), so this figure overlaps process RSS — the caller
// flags that so the UI can caption it rather than double-counting silently.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <string>

namespace pinpoint::gpumetrics::detail {

bool macInit(std::string &name, uint64_t &total, bool &unified)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev)
            return false;
        const char *n = [[dev name] UTF8String];
        name    = n ? n : "Metal device";
        total   = static_cast<uint64_t>([dev recommendedMaxWorkingSetSize]);
        unified = [dev hasUnifiedMemory];
        return true;
    }
}

uint64_t macProcessBytes()
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev)
            return 0;
        return static_cast<uint64_t>([dev currentAllocatedSize]);
    }
}

} // namespace pinpoint::gpumetrics::detail
