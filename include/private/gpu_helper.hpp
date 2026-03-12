#pragma once

#include "transcoder.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

#include <unistd.h>
#include <stdio.h>
#include <iostream>

namespace transcoder
{

    struct GpuContext
    {
        AVBufferRef *hw_device_ctx = nullptr; // null = CPU mode
        AVHWDeviceType device_type = AV_HWDEVICE_TYPE_NONE;
        AVPixelFormat pix_fmt = AV_PIX_FMT_NONE; // surface fmt for encoder

        bool is_gpu() const { return hw_device_ctx != nullptr; }
    };

    class GpuHelper
    {
    public:
        bool initialize(GpuContext &gpuContext, HWDeviceType hwDeviceType)
        {
            if (hwDeviceType == HWDeviceType::VAAPI)
            {
                if (try_vaapi(&gpuContext.hw_device_ctx))
                {
                    gpuContext.device_type = AV_HWDEVICE_TYPE_VAAPI;
                    gpuContext.pix_fmt = AV_PIX_FMT_VAAPI;
                    return true;
                }
                else
                {
                    std::cout << "[GPU Helper] ERROR: No VAPPI device found.\n";
                    return false;
                }
            }
            else if (hwDeviceType == HWDeviceType::NONE)
            {
                std::cout << "[GPU Helper] ERROR: Hardware devide type is NONE.\n";
                return false;
            }

            return true;
        }

    private:
        bool try_hwdevice(AVHWDeviceType type, const char *device, AVBufferRef **outCtx)
        {
            int r = av_hwdevice_ctx_create(outCtx, type, device, nullptr, 0);
            if (r >= 0)
                return true;
            if (*outCtx)
            {
                av_buffer_unref(outCtx);
                *outCtx = nullptr;
            }
            return false;
        }

        bool try_vaapi(AVBufferRef **outCtx)
        {
            for (int i = 128; i <= 135; i++)
            {
                char path[32];
                snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
                if (access(path, F_OK) != 0)
                    continue;
                if (try_hwdevice(AV_HWDEVICE_TYPE_VAAPI, path, outCtx))
                {
                    std::cout << "[GPU Helper] VAAPI device found: " << path << "\n";
                    return true;
                }
            }
            return false;
        }
    };

}