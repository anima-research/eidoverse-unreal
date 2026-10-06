#include "EidoverseSTT.h"
#if !PLATFORM_MAC
struct FEidoStt::FImpl {};
FEidoStt::EAuth FEidoStt::Authorization() { return EAuth::Unsupported; }
void FEidoStt::RequestAuthorization(TFunction<void(bool)> Done) { Done(false); }
FEidoStt::FEidoStt() {}
FEidoStt::~FEidoStt() {}
bool FEidoStt::Start(FString& OutError) { OutError = TEXT("speech-to-text isn't supported on this platform"); return false; }
void FEidoStt::Stop() {}
bool FEidoStt::IsRunning() const { return false; }
bool FEidoStt::IsOnDevice() const { return false; }
void FEidoStt::Feed(const float*, int32, double) {}
void FEidoStt::Poll(TArray<FString>& F, FString& E) { F.Reset(); E.Empty(); }
#endif
