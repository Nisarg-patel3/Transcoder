
#include "transcoder.hpp"

namespace transcoder
{
    std::string videoCodecTypeToString(VideoCodecType c, bool pretty)
    {
        if (!pretty)
        {
            switch (c)
            {
            case VideoCodecType::H264:
                return "h264";
            case VideoCodecType::H265:
                return "h265";
            case VideoCodecType::MJPEG:
                return "mjpeg";
            case VideoCodecType::MPEG4:
                return "mpeg4";
            case VideoCodecType::VP9:
                return "vp9";
            case VideoCodecType::AV1:
                return "av1";
            default:
                return "unknown";
            }
        }
        else
        {
            switch (c)
            {
            case VideoCodecType::H264:
                return "H.264";
            case VideoCodecType::H265:
                return "H.265";
            case VideoCodecType::MJPEG:
                return "MJPEG";
            case VideoCodecType::MPEG4:
                return "MPEG-4";
            case VideoCodecType::VP9:
                return "VP9";
            case VideoCodecType::AV1:
                return "AV1";
            default:
                return "Unknown";
            }
        }
    }

    std::string audioCodecTypeToString(AudioCodecType c, bool pretty)
    {
        if (!pretty)
        {
            switch (c)
            {
            case AudioCodecType::AAC:
                return "aac";
            case AudioCodecType::MP3:
                return "mp3";
            case AudioCodecType::AC3:
                return "ac3";
            case AudioCodecType::EAC3:
                return "eac3";
            case AudioCodecType::DTS:
                return "dts";
            case AudioCodecType::OPUS:
                return "opus";
            case AudioCodecType::VORBIS:
                return "vorbis";
            case AudioCodecType::PCM:
                return "pcm";
            case AudioCodecType::FLAC:
                return "flac";
            case AudioCodecType::ALAC:
                return "alac";
            case AudioCodecType::MP2:
                return "mp2";
            case AudioCodecType::SPEEX:
                return "speex";
            case AudioCodecType::WMAV2:
                return "wmav2";
            default:
                return "unknown";
            }
        }
        else
        {
            switch (c)
            {
            case AudioCodecType::AAC:
                return "AAC";
            case AudioCodecType::MP3:
                return "MP3";
            case AudioCodecType::AC3:
                return "AC3";
            case AudioCodecType::EAC3:
                return "E-AC3";
            case AudioCodecType::DTS:
                return "DTS";
            case AudioCodecType::OPUS:
                return "Opus";
            case AudioCodecType::VORBIS:
                return "Vorbis";
            case AudioCodecType::PCM:
                return "PCM";
            case AudioCodecType::FLAC:
                return "FLAC";
            case AudioCodecType::ALAC:
                return "ALAC";
            case AudioCodecType::MP2:
                return "MP2";
            case AudioCodecType::SPEEX:
                return "Speex";
            case AudioCodecType::WMAV2:
                return "WMA v2";
            default:
                return "Unknown";
            }
        }
    }

    std::string containerFormatToString(ContainerFormat f)
    {
        switch (f)
        {
        case ContainerFormat::MP4:
            return "MP4";
        case ContainerFormat::MKV:
            return "MKV";
        case ContainerFormat::AVI:
            return "AVI";
        case ContainerFormat::MPEG_TS:
            return "MPEG-TS";
        case ContainerFormat::FLV:
            return "FLV";
        case ContainerFormat::MOV:
            return "MOV";
        default:
            return "Unknown";
        }
    }

    std::string containerFormatToExtension(ContainerFormat f)
    {
        switch (f)
        {
        case ContainerFormat::MP4:
            return ".mp4";
        case ContainerFormat::MKV:
            return ".mkv";
        case ContainerFormat::AVI:
            return ".avi";
        case ContainerFormat::MPEG_TS:
            return ".ts";
        case ContainerFormat::FLV:
            return ".flv";
        case ContainerFormat::MOV:
            return ".mov";
        default:
            return "unknown";
        }
    }

    std::string listSupportedExtensions()
    {
        std::string list = "";
        for (int i = (int)ContainerFormat::FIRST; i <= (int)ContainerFormat::LAST; i++)
        {
            if (i)
                list += "  ";
            list += containerFormatToExtension((ContainerFormat)i);
        }
        return list;
    }

    std::string listSupportedVideoCodecs(ContainerFormat container)
    {
        std::string list = "";
        for (int i = (int)VideoCodecType::FIRST; i <= (int)VideoCodecType::LAST; i++)
        {
            if (i)
                list += ", ";
            if (isVideoCodecSupportedInContainer((VideoCodecType)i, container))
                list += videoCodecTypeToString((VideoCodecType)i);
        }
        return list;
    }

    std::string listSupportedVideoCodecs()
    {
        std::string list = "";
        for (int i = (int)VideoCodecType::FIRST; i <= (int)VideoCodecType::LAST; i++)
        {
            if (i)
                list += ", ";
            list += videoCodecTypeToString((VideoCodecType)i);
        }
        return list;
    }

    std::string listSupportedAudioCodecs(ContainerFormat container)
    {
        std::string list = "";
        for (int i = (int)AudioCodecType::FIRST; i <= (int)AudioCodecType::LAST; i++)
        {
            if (i)
                list += ", ";
            if (isAudioCodecSupportedInContainer((AudioCodecType)i, container))
                list += audioCodecTypeToString((AudioCodecType)i);
        }
        return list;
    }

    std::string listSupportedAudioCodecs()
    {
        std::string list = "";
        for (int i = (int)AudioCodecType::FIRST; i <= (int)AudioCodecType::LAST; i++)
        {
            if (i)
                list += ", ";
            list += audioCodecTypeToString((AudioCodecType)i);
        }
        return list;
    }

    std::string sourceTypeToString(SourceType t)
    {
        switch (t)
        {
        case SourceType::FILE:
            return "FILE";
        case SourceType::RTSP:
            return "RTSP Stream";
        default:
            return "Unknown";
        }
    }

    std::string bitrateModeToString(BitrateMode m)
    {
        switch (m)
        {
        case BitrateMode::CBR:
            return "CBR";
        case BitrateMode::VBR:
            return "VBR";
        case BitrateMode::CQP:
            return "CQP";
        default:
            return "Unknown";
        }
    }

    std::string hwDeviceTypeToString(HWDeviceType t)
    {
        switch (t)
        {
        case HWDeviceType::NONE:
            return "CPU";
        case HWDeviceType::VAAPI:
            return "VAAPI (Intel)";
        default:
            return "Unknown";
        }
    }

    bool isStreamUrl(const std::string &p)
    {
        return p.rfind("rtsp://", 0) == 0 || p.rfind("rtsps://", 0) == 0;
    }

    ContainerFormat detectContainerFromFilePath(const std::string &filePath)
    {
        auto ext = filePath.substr(filePath.rfind('.') + 1);

        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == "mp4")
            return ContainerFormat::MP4;

        if (ext == "mkv")
            return ContainerFormat::MKV;

        if (ext == "avi")
            return ContainerFormat::AVI;

        if (ext == "ts" || ext == "mts" || ext == "m2ts")
            return ContainerFormat::MPEG_TS;

        if (ext == "flv")
            return ContainerFormat::FLV;

        if (ext == "mov")
            return ContainerFormat::MOV;

        return ContainerFormat::UNKNOWN;
    }

    bool isVideoCodecSupportedInContainer(VideoCodecType codec, ContainerFormat container)
    {
        switch (container)
        {
        case ContainerFormat::MP4:
            return codec == VideoCodecType::H264 ||
                   codec == VideoCodecType::H265 ||
                   codec == VideoCodecType::MPEG4 ||
                   codec == VideoCodecType::MJPEG;

        case ContainerFormat::MOV:
            return codec == VideoCodecType::H264 ||
                   codec == VideoCodecType::H265 ||
                   codec == VideoCodecType::MPEG4 ||
                   codec == VideoCodecType::MJPEG;

        case ContainerFormat::MKV:
            // MKV supports all codecs in your enum
            return codec >= VideoCodecType::FIRST &&
                   codec <= VideoCodecType::LAST;

        case ContainerFormat::AVI:
            return codec == VideoCodecType::H264 ||
                   codec == VideoCodecType::MPEG4 ||
                   codec == VideoCodecType::MJPEG;

        case ContainerFormat::MPEG_TS:
            return codec == VideoCodecType::H264 ||
                   codec == VideoCodecType::H265 ||
                   codec == VideoCodecType::MPEG4 ||
                   codec == VideoCodecType::AV1;

        case ContainerFormat::FLV:
            return codec == VideoCodecType::H264 ||
                   codec == VideoCodecType::MPEG4;

        default:
            return false;
        }
    }

    bool isAudioCodecSupportedInContainer(AudioCodecType codec, ContainerFormat container)
    {
        switch (container)
        {
        case ContainerFormat::MP4:
            return codec == AudioCodecType::AAC ||
                   codec == AudioCodecType::MP3 ||
                   codec == AudioCodecType::AC3 ||
                   codec == AudioCodecType::EAC3 ||
                   codec == AudioCodecType::ALAC ||
                   codec == AudioCodecType::FLAC;

        case ContainerFormat::MOV:
            return codec == AudioCodecType::AAC ||
                   codec == AudioCodecType::MP3 ||
                   codec == AudioCodecType::AC3 ||
                   codec == AudioCodecType::EAC3 ||
                   codec == AudioCodecType::ALAC ||
                   codec == AudioCodecType::PCM;

        case ContainerFormat::MKV:
            // MKV supports all codecs in your enum
            return codec >= AudioCodecType::FIRST &&
                   codec <= AudioCodecType::LAST;

        case ContainerFormat::AVI:
            return codec == AudioCodecType::MP3 ||
                   codec == AudioCodecType::AC3 ||
                   codec == AudioCodecType::DTS ||
                   codec == AudioCodecType::PCM ||
                   codec == AudioCodecType::MP2 ||
                   codec == AudioCodecType::WMAV2;

        case ContainerFormat::MPEG_TS:
            return codec == AudioCodecType::AAC ||
                   codec == AudioCodecType::MP3 ||
                   codec == AudioCodecType::AC3 ||
                   codec == AudioCodecType::EAC3 ||
                   codec == AudioCodecType::DTS ||
                   codec == AudioCodecType::MP2 ||
                   codec == AudioCodecType::OPUS;

        case ContainerFormat::FLV:
            return codec == AudioCodecType::AAC ||
                   codec == AudioCodecType::MP3;

        default:
            return false;
        }
    }
}
