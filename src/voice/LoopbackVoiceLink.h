#pragma once
#include "shared/VoiceLink.h"
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

class LoopbackVoiceLink;

// Liga varias LoopbackVoiceLink em memoria: o que uma envia, todas as outras recebem.
class LoopbackVoiceHub {
public:
    void join(LoopbackVoiceLink* link);
    void leave(LoopbackVoiceLink* link);
    void broadcast(const LoopbackVoiceLink* from, const VoiceFrame& frame);

private:
    std::mutex mutex_;
    std::vector<LoopbackVoiceLink*> links_;
};

// VoiceLink em memoria, para testes e demonstracao.
//  - Com um hub: conversa entre varios VoiceChat no mesmo processo.
//  - Em eco: devolve a propria voz como se fosse do jogador "echoAs" (usado no --voz-teste).
class LoopbackVoiceLink : public VoiceLink {
public:
    explicit LoopbackVoiceLink(std::shared_ptr<LoopbackVoiceHub> hub);
    static LoopbackVoiceLink echo(PlayerId echoAs);
    ~LoopbackVoiceLink() override;
    LoopbackVoiceLink(LoopbackVoiceLink&& other) noexcept;
    LoopbackVoiceLink(const LoopbackVoiceLink&) = delete;
    LoopbackVoiceLink& operator=(const LoopbackVoiceLink&) = delete;
    LoopbackVoiceLink& operator=(LoopbackVoiceLink&&) = delete;

    void sendVoice(const VoiceFrame& frame) override;
    std::vector<VoiceFrame> receiveVoice() override;

    void deliver(const VoiceFrame& frame);   // Chamado pelo hub.

private:
    LoopbackVoiceLink() = default;

    std::shared_ptr<LoopbackVoiceHub> hub_;
    PlayerId echoAs_ = kNoPlayer;
    std::mutex mutex_;
    std::deque<VoiceFrame> inbox_;
};
