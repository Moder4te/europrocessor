// ================================================================
//  ISaver.h — 화면보호기 설정 인터페이스
//  ----------------------------------------------------------------
//  WebServer가 화면보호기 enable/timeout 을 읽고 쓰기 위한 추상 경계.
//  디스플레이 빌드에선 DisplayUI가 구현, 무디스플레이 빌드에선 stub.
// ================================================================
#pragma once

class ISaver {
public:
    virtual ~ISaver() = default;
    virtual bool saverEnabled() const   = 0;
    virtual int  saverTimeoutSec() const = 0;
    virtual void setSaverEnabled(bool en) = 0;
    virtual void setSaverTimeoutSec(int s) = 0;
};

// 무디스플레이 빌드용 stub (화면보호기 없음)
class NullSaver : public ISaver {
public:
    bool saverEnabled() const override    { return false; }
    int  saverTimeoutSec() const override { return 60; }
    void setSaverEnabled(bool) override   {}
    void setSaverTimeoutSec(int) override {}
};
