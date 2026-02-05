//
// Created by laojk on 2025-11-22.
//

#ifndef DASH_N_BARK_AUDIOBUFFER_H
#define DASH_N_BARK_AUDIOBUFFER_H

#include <string>
#include <memory>

namespace AudioMixer {

    class AudioBuffer {
    public:
        enum AudioFormat {
            PCM_16BIT_STEREO_48K,
            // Add more formats as needed
        };

        AudioBuffer(const std::string& file_path, AudioFormat format);
        AudioBuffer(const uint8_t* data, size_t size);
        AudioBuffer(size_t size);
        ~AudioBuffer();

        [[nodiscard]] bool isValid() const;
        [[nodiscard]] uint8_t* getData();
        [[nodiscard]] size_t getSize() const;
    private:
        // audio info
        uint8_t *data_;
        size_t size_;
        AudioFormat format_;

        friend class AudioMixer;
        [[nodiscard]] uint8_t *getDataWritable() const;

        [[nodiscard]] bool checkFormatValidation() const;
    };

    typedef std::shared_ptr<AudioBuffer> AudioBufferPtr;

    class AudioClip {
    public:
        AudioClip(const std::string& file_path, AudioBuffer::AudioFormat format, std::string name="");
        AudioClip(const AudioBufferPtr& buffer, std::string name="");
        AudioClip(const AudioBufferPtr& buffer, size_t start, size_t size, std::string name="");
        AudioClip(const AudioClip& clip, size_t start, size_t size);

        [[nodiscard]] uint8_t* getData();
        [[nodiscard]] size_t getSize() const;
        [[nodiscard]] const std::string& getName() const;

        [[nodiscard]] AudioClip subClip(size_t start_sub, size_t size_sub) const;

    private:
        AudioBufferPtr buffer;
        size_t start;
        size_t size;
        std::string name;
    };

    typedef std::shared_ptr<AudioClip> AudioClipPtr;

} // namespace AudioMixer


#endif //DASH_N_BARK_AUDIOBUFFER_H