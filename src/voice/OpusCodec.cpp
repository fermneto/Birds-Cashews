#include "voice/OpusCodec.h"
#include "shared/VoiceLink.h"
#include <opus.h>
#include <algorithm>
#include <iostream>

VoiceEncoder::VoiceEncoder(int bitrate, int complexity) {
    int error = OPUS_OK;
    encoder_ = opus_encoder_create(kVoiceRate, 1, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        std::cerr << "Opus indisponivel, voz desligada: " << opus_strerror(error) << "\n";
        encoder_ = nullptr;
        return;
    }
    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(complexity));
    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(10));
}

VoiceEncoder::~VoiceEncoder() { if (encoder_) opus_encoder_destroy(encoder_); }

std::vector<uint8_t> VoiceEncoder::encode(const float* frame) {
    if (!encoder_) return {};
    std::vector<uint8_t> packet(kMaxVoicePayload);
    const int bytes = opus_encode_float(encoder_, frame, kVoiceFrame, packet.data(), static_cast<opus_int32>(packet.size()));
    if (bytes <= 0) return {};
    packet.resize(static_cast<size_t>(bytes));
    return packet;
}

VoiceDecoder::VoiceDecoder() {
    int error = OPUS_OK;
    decoder_ = opus_decoder_create(kVoiceRate, 1, &error);
    if (error != OPUS_OK) decoder_ = nullptr;
}

VoiceDecoder::~VoiceDecoder() { if (decoder_) opus_decoder_destroy(decoder_); }

void VoiceDecoder::reset() { if (decoder_) opus_decoder_ctl(decoder_, OPUS_RESET_STATE); }

bool VoiceDecoder::decode(const std::vector<uint8_t>& payload, float* out) {
    int samples = -1;
    if (decoder_ && !payload.empty())
        samples = opus_decode_float(decoder_, payload.data(), static_cast<opus_int32>(payload.size()), out, kVoiceFrame, 0);
    if (samples < 0) samples = 0;
    std::fill(out + samples, out + kVoiceFrame, 0.0f);
    return samples == kVoiceFrame;
}

void VoiceDecoder::conceal(const std::vector<uint8_t>* next, float* out) {
    int samples = -1;
    if (decoder_ && next && !next->empty())
        samples = opus_decode_float(decoder_, next->data(), static_cast<opus_int32>(next->size()), out, kVoiceFrame, 1);
    if (decoder_ && samples < 0) samples = opus_decode_float(decoder_, nullptr, 0, out, kVoiceFrame, 0);
    if (samples < 0) samples = 0;
    std::fill(out + samples, out + kVoiceFrame, 0.0f);
}
