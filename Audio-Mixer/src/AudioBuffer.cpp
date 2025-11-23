//
// Created by laojk on 2025-11-22.
//

#include "Audio-Mixer/AudioBuffer.h"

#include <cstring>
#include <fstream>

namespace AudioMixer {

    AudioBuffer::AudioBuffer(const std::string& file_path, AudioFormat format)
        : data(nullptr), size(0), format(format) {
        std::ifstream input(file_path, std::ios::in | std::ios::binary | std::ios::ate);
        if (input.is_open()) {
            size = input.tellg();
            data = new uint8_t[size];
            input.seekg(0, std::ios::beg);
            input.read((char *) data, size);
            input.close();
        } else {
            throw std::runtime_error("Failed to open `" + file_path + "`");
        }
        if (!checkFormatValidation()) {
            throw std::runtime_error("Audio data in `" + file_path + "` does not conform to the specified format");
        }
    }

    AudioBuffer::AudioBuffer(size_t size)
        : data(new uint8_t[size]), size(size), format(PCM_16BIT_STEREO_44K) {
        memset(data, 0, size);
    }

    AudioBuffer::~AudioBuffer() {
        delete[] data;
    }

    bool AudioBuffer::isValid() const {
        return data != nullptr && size > 0;
    }

    const uint8_t* AudioBuffer::getData() const {
        return data;
    }

    size_t AudioBuffer::getSize() const {
        return size;
    }

    uint8_t *AudioBuffer::getDataWritable() const {
        return data;
    }

    bool AudioBuffer::checkFormatValidation() const {
        switch (format) {
            case PCM_16BIT_STEREO_44K:
                return (size % 4) == 0; // 16-bit stereo means each sample is 4 bytes
            default:
                return false;
        }
    }

    AudioClip::AudioClip(const std::string& file_path, AudioBuffer::AudioFormat format) {
        buffer = std::make_shared<AudioBuffer>(file_path, format);
        start = 0;
        size = buffer->getSize();
    }

    AudioClip::AudioClip(const AudioBufferPtr& buffer)
        : buffer(buffer), start(0), size(buffer->getSize()) {
    }

    AudioClip::AudioClip(const AudioBufferPtr& buffer, size_t start, size_t size)
        : buffer(buffer), start(start), size(size) {
        if (start + size > buffer->getSize()) {
            throw std::out_of_range("AudioClip range exceeds buffer size");
        }
    }

    AudioClip::AudioClip(const AudioClip& clip, size_t start, size_t size)
        : buffer(clip.buffer), start(clip.start + start), size(size) {
        if (this->start + size > clip.start + clip.size) {
            throw std::out_of_range("AudioClip range exceeds original clip size");
        }
    }

    const uint8_t* AudioClip::getData() const {
        return buffer->getData() + start;
    }

    size_t AudioClip::getSize() const {
        return size;
    }

    AudioClip AudioClip::subClip(size_t start_sub, size_t size_sub) const {
        return {*this, start_sub, size_sub};
    }

} // namespace AudioMixer
