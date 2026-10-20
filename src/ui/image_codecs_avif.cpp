#include "ui/image_codecs.h"

#include "include/codec/SkAvifDecoder.h"
#include "include/codec/SkCodec.h"

// AVIF를 켠 구성이다 (`LUIL_ENABLE_AVIF`). 이 파일이 libavif·libgav1·libyuv를 끌어온다.
namespace luil {
    void register_optional_image_decoders()
    {
        // 정지 AVIF도 움직이는 AVIF도 이 디코더 하나가 읽는다 — 장 수와 시간은 SkCodec의
        // 여러 장 API(`getFrameCount`·`getFrameInfo`)로 나온다.
        static const bool registered { (SkCodecs::Register(SkAvifDecoder::LibAvif::Decoder()), true) };
        static_cast<void>(registered);
    }
} // namespace luil
