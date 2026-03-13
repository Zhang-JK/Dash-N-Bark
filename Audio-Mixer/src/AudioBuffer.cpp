//
// Created by laojk on 2025-11-22.
//

#include "Audio-Mixer/AudioBuffer.h"

#include <cstring>
#include <fstream>
#include <utility>

namespace AudioMixer {

    AudioBuffer::AudioBuffer(const std::string& file_path, AudioFormat format)
        : data_(nullptr), size_(0), format_(format) {
        std::ifstream input(file_path, std::ios::in | std::ios::binary | std::ios::ate);
        if (input.is_open()) {
            size_ = input.tellg();
            data_ = new uint8_t[size_];
            input.seekg(0, std::ios::beg);
            input.read((char *) data_, size_);
            input.close();
        } else {
            throw std::runtime_error("Failed to open `" + file_path + "`");
        }
        if (!checkFormatValidation()) {
            throw std::runtime_error("Audio data in `" + file_path + "` does not conform to the specified format");
        }
    }

    AudioBuffer::AudioBuffer(const uint8_t* data, size_t size)
        : data_(new uint8_t[size]), size_(size), format_(PCM_16BIT_STEREO_48K) {
        memcpy(this->data_, data, size);
    }

    AudioBuffer::AudioBuffer(size_t size)
        : data_(new uint8_t[size]), size_(size), format_(PCM_16BIT_STEREO_48K) {
        memset(data_, 0, size);
    }

    AudioBuffer::~AudioBuffer() {
        delete[] data_;
    }

    bool AudioBuffer::isValid() const {
        return data_ != nullptr && size_ > 0;
    }

    uint8_t* AudioBuffer::getData() {
        return data_;
    }

    size_t AudioBuffer::getSize() const {
        return size_;
    }

    uint8_t *AudioBuffer::getDataWritable() const {
        return data_;
    }

    bool AudioBuffer::checkFormatValidation() const {
        switch (format_) {
            case PCM_16BIT_STEREO_48K:
                return (size_ % 4) == 0; // 16-bit stereo means each sample is 4 bytes
            default:
                return false;
        }
    }

    AudioClip::AudioClip(const std::string& file_path, AudioBuffer::AudioFormat format, std::string name) {
        buffer = std::make_shared<AudioBuffer>(file_path, format);
        start = 0;
        size = buffer->getSize();
        this->name = std::move(name);
    }

    AudioClip::AudioClip(const AudioBufferPtr& buffer, std::string name)
        : buffer(buffer), start(0), size(buffer->getSize()), name(std::move(name)) {
    }

    AudioClip::AudioClip(const AudioBufferPtr& buffer, size_t start, size_t size, std::string name)
        : buffer(buffer), start(start), size(size), name(std::move(name)) {
        if (start + size > buffer->getSize()) {
            throw std::out_of_range("AudioClip range exceeds buffer size");
        }
    }

    AudioClip::AudioClip(const AudioClip& clip, size_t start, size_t size)
        : buffer(clip.buffer), start(clip.start + start), size(size), name(clip.name) {
        if (this->start + size > clip.start + clip.size) {
            throw std::out_of_range("AudioClip range exceeds original clip size");
        }
    }

    uint8_t* AudioClip::getData() {
        return buffer->getData() + start;
    }

    size_t AudioClip::getSize() const {
        return size;
    }

    const std::string& AudioClip::getName() const {
        return name;
    }

    AudioClip AudioClip::subClip(size_t start_sub, size_t size_sub) const {
        return {*this, start_sub, size_sub};
    }

} // namespace AudioMixer
