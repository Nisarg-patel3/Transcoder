/**
 * Example 1: Basic File Transcoding
 * ==================================
 * Demonstrates the simplest use of the library:
 *   H.264 → H.265 with default settings
 */

#include <transcoder.hpp>
#include <iostream>

using namespace transcoder;

int main(int argc, char* argv[]) 
{
    
    if(argc<2)
    {
        std::cout << "Incorrect usage. Correct usage: ./example_1 h264_filename \n";
        return 0;
    }

    int count = 0;

    std::string outputPath = "output_h265";
    // ── Configure ─────────────────────────────────────────────
    PipelineConfig config;
    config.inputPath    = argv[1];         // change to your file
    config.keepOriginalVideoCodec = false;
    config.targetVideoCodec  = VideoCodecType::H265;
    config.bitrateMode  = BitrateMode::CBR;
    config.targetBitrate= 5'551'000;            // 3 Mbps
    config.passAudio    = false;
    config.enableStats  = true;

    config.outputPath   = outputPath  + ".mp4";

    // ── Build and Run ─────────────────────────────────────────
    Pipeline pipeline(config);

    if (!pipeline.initialize()) {
        std::cout << "Failed to initialize pipeline\n";
        return 1;
    }

    std::cout << "Transcoding " << config.inputPath << " → " << config.outputPath << "\n";

    bool ok = pipeline.run();

    if (ok) {
        auto* stats = pipeline.getPerfMonitor();
        std::cout << "Done! Encoded " << stats->totalFramesEncoded() << " frames, " << stats->totalBytesOutput() / 1024 / 1024 << " MB output\n";
    }

    return ok;
}
