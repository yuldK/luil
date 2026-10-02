# HTTP client

`luil::net::http_client`는 admission 제한, 명시적 취소, heartbeat scheduling, response body 분류와 eager parsing을 제공하는 비동기 WinHTTP client다. 전용 processing thread 하나를 소유하므로 network callback은 짧게 끝나며 JSON, text, image 처리가 UI 또는 logic thread에서 실행되지 않는다.

공개 API는 [`include/luil/net/http_client.h`](../include/luil/net/http_client.h), [`include/luil/net/http_message.h`](../include/luil/net/http_message.h), [`include/luil/net/http_body.h`](../include/luil/net/http_body.h), [`include/luil/net/http_media_type.h`](../include/luil/net/http_media_type.h)에 나뉘어 있다.

## Client 생성

```cpp
luil::net::http_client_config config {};
config.deliver = [&host](luil::net::http_response response) {
    host.post_app_message(luil::app_message { fetch_finished { std::move(response) } });
};

std::u8string error;
auto client = luil::net::http_client::create(std::move(config), error);
```

`create`는 WinHTTP session을 열고 processing thread를 시작한다. 실패하면 `nullptr`을 반환하고 `error`에 진단을 기록한다. 성공 시 `error`는 변경하지 않는다. 일반적으로 application당 client 하나면 충분하며 connection은 host, port, security mode별로 내부 cache된다.

`http_client_config`는 다음을 설정한다.

- 유일한 response callback인 `deliver`
- 기본값이 `luil`인 `user_agent`
- system proxy 사용 여부
- active WinHTTP request handle 수를 제한하는 `max_in_flight`(기본 32)
- accepted ticket 중 delivery가 끝나지 않은 수를 제한하는 `max_pending_responses`(기본 128)
- text transcoder override
- 기본 1초인 제한된 `stop_budget`

모든 공개 method는 thread-safe다. `stop()`만 blocking한다. `deliver`는 `stop()`이 join하는 thread에서 실행되므로 callback 안에서 `stop()`을 호출하면 안 된다.

## Request 전송과 backpressure

`http_request`는 client가 복사하는 value다. 절대 `http` 또는 `https` URL, 고정 method enum, header, 선택적 body와 content type, parsing option, body 제한, redirect policy, decompression 설정, timeout을 포함한다.

```cpp
luil::net::http_request request {};
request.url = u8"https://example.test/api/items";
request.method = luil::net::http_method::post;
request.content_type = u8"application/json";
request.body = luil::net::http_text_body(u8R"({"name":"sample"})");
request.total_timeout = std::chrono::seconds { 20 };

std::u8string error;
luil::net::http_ticket ticket = client->send(std::move(request), error);
```

0이 아닌 ticket은 request가 accepted되었으며 shutdown이 delivery를 이미 비활성화하지 않았다면 정확히 하나의 response가 `deliver`에 제공된다는 뜻이다. 0은 synchronous admission 실패이며 이후 response가 없다. 잘못된 URL, scheme, header, 정지된 client, active transport 포화, pending response 포화가 이에 해당한다. Acceptance 이전 실패는 `send`가 반환하고, 이후 완료와 transport 실패는 `http_response::error`로 전달한다.

`max_in_flight`는 숨은 queue를 만들지 않는다. Active handle 제한을 넘는 send는 즉시 거부된다. `max_pending_responses`는 accepted one-shot request 또는 heartbeat 전체마다 최종 delivery가 반환될 때까지 slot을 예약한다. 느린 `deliver`는 완료 값을 무제한 쌓는 대신 새 admission에 backpressure를 건다. Idle heartbeat도 미래의 최종 response를 약속하므로 slot을 유지한다.

`in_flight()`는 accepted되었지만 아직 delivery가 끝나지 않은 request를 보고한다. Heartbeat에서는 현재 active round만 센다. Parsing 중이거나 `deliver` 안에 있는 response도 포함하며 callback 반환 뒤에 감소한다.

## Method, header, upload body

GET, HEAD, POST, PUT, PATCH, DELETE(`http_method::remove`), OPTIONS를 지원한다. `http_method_name`은 wire format 이름을 반환한다.

Header 이름은 유효한 HTTP token character만 포함해야 한다. 이름이나 값에 CR, LF, NUL이 있거나 header text가 올바른 UTF-8이 아니면 network 동작 전에 거부한다. User header는 같은 이름의 기존 값을 대체한다.

Body가 있으면 `content_type`이 `Content-Type` header가 된다. 비어 있으면 명시된 `Content-Type`을 유지하고, 그것도 없으면 `application/octet-stream`을 쓴다. `Host`와 `Content-Length`는 WinHTTP에 맡겨야 한다. WinHTTP의 `DWORD` 길이 범위를 넘는 upload body는 전송 전에 거부한다.

## Timeout과 redirect

Connect, send, receive timeout은 각 단계의 inactivity를 제한한다. `receive_timeout`은 전체 request 제한이 아니라 수신 조각 사이의 간격 제한이다. 시작부터 body 완료까지의 경과 시간은 `total_timeout`이 제한하며, 계속 소량의 byte를 보내는 server를 멈출 수 있는 유일한 설정이다. 0이면 total deadline을 끈다.

Total deadline은 단일 processing thread가 검사하므로 긴 parsing이나 다른 callback이 관측을 조금 늦출 수 있다. 이는 실시간 interrupt가 아니라 deadline policy다.

Body 없는 request는 기본적으로 `max_redirects`까지 redirect를 따른다. HTTPS에서 HTTP로의 downgrade는 따르지 않는다. `http_redirect_policy::none`은 `Location` header를 포함한 3xx response를 반환한다.

Upload body가 하나라도 있으면 policy와 관계없이 redirect를 따르지 않는다. WinHTTP가 redirect request에 body를 신뢰성 있게 재전송하지 못하므로 application이 3xx와 `Location`을 확인해 새 request 여부를 결정해야 한다.

`decompress`는 OS가 지원할 때 WinHTTP gzip과 deflate decoding을 켠다. 자동 decompression 미지원은 request 실패가 아니다.

## Response 계약

`http_response`는 ticket, heartbeat sequence, `last`, status와 reason, final URL, header, parsed content type, body, transport error, network elapsed time을 가진다. `elapsed`는 body 수신 완료 시점까지이며 parsing 시간은 제외한다.

HTTP 4xx와 5xx는 transport error가 아니라 유효한 response다. `error.empty()`는 transport 성공 여부를 나타내고, `ok()`는 추가로 2xx status를 요구한다. Error status의 body도 정상적으로 parse해 server 진단을 표시할 수 있다.

Status 0은 status line을 받지 못했다는 뜻이다. Transport error는 invalid URL, unsupported scheme, invalid header, name resolution, connection, truncated connection, TLS, timeout, cancellation, redirect limit, body limit, stop, 기타 system error로 분류한다. 가능한 경우 `native_error`에 platform code를 보존한다.

Transport 실패 시 partial body는 공개하지 않는다. 일부 byte를 정상적인 짧은 response와 구분할 수 없으므로 폐기한다.

## Body 제한과 parsing

`max_body_bytes` 기본값은 64 MiB다. 선언된 `Content-Length`가 제한을 넘으면 읽기 전에 거부하며, 길이가 없거나 부정확하면 누적 byte로 검사한다. Decompression 시에는 WinHTTP가 제공하는 expanded byte에 제한을 적용한다. HEAD처럼 의미상 body가 없는 response는 header가 resource 길이를 나타내더라도 실패하지 않는다.

Transport가 끝나면 delivery 전에 `decode_http_body`를 호출한다. `http_body`는 원래 response byte를 shared 형태로 항상 보존하며, shared JSON, immutable image frame 또는 shared UTF-8 text도 포함할 수 있다. Parsing 오류는 `parse_error`에 남고 `http_response::error`를 바꾸거나 선택된 kind를 bytes로 되돌리지 않는다.

분류 순서는 다음과 같다.

1. 빈 byte는 `empty`다.
2. `parse.assume_kind`가 있으면 우선한다.
3. Content type이 없거나 invalid, empty, `application/octet-stream`이면 image signature sniffing을 허용한다.
4. 그 밖에는 선언된 media type을 따른다.

JSON에는 `application/json`, `text/json`, `+json` suffix가 포함된다. 설정된 decoder가 지원하지 않는 SVG는 image type에서 제외한다. HTML은 `html_as_text`가 설정되지 않으면 charset 정보가 붙은 bytes로 남는다. 그 밖의 text, XML, JavaScript, form-urlencoded는 UTF-8 text가 된다. 알 수 없는 type은 오류 없이 bytes로 남는다.

Sniffing은 PNG, JPEG, GIF, WebP, BMP signature만 인식한다. JSON이나 text를 추측하지 않고 명확한 declared type을 덮어쓰지 않는다. 잘못된 server header를 아는 application은 `assume_kind`를 명시할 수 있다.

JSON은 byte와 nesting depth 제한을 별도로 적용한다. Text는 UTF-8과 ASCII를 직접 받고, 잘못된 UTF-8은 U+FFFD로 대체하면서 parsing warning을 기록한다. 그 밖의 charset은 `http_client_config::transcode`를 사용한다. 기본 Windows code-page transcoder는 EUC-KR, Shift-JIS, Windows-1252, UTF-16 등을 처리한다. 모르는 charset은 byte를 보존하고 이유를 기록한다.

Image parsing은 [이미지 디코딩](image-decode-design.md)과 같은 제한과 보장을 사용한다. `parse.image.animated = false`는 thumbnail용 still frame 하나만 decode한다.

## Delivery thread

`deliver`는 client-owned processing thread에서 직렬로 실행된다. Callback은 짧게 유지하고 response를 thread-safe application channel로 옮겨야 한다. `app_host::post_app_message`가 기본 bridge다.

Callback에서 `send`, `cancel`, `start_heartbeat`를 호출할 수 있다. Exception은 잡히며 해당 response는 폐기되므로 callback은 가능한 한 throw하지 않아야 한다. 빈 callback은 response를 조용히 폐기한다.

## Heartbeat

`start_heartbeat(request, schedule, error)`는 ticket 하나로 반복 flow를 만든다. Sequence는 1부터 시작하고 `last`는 더 이상 response가 오지 않음을 나타낸다.

`interval`은 고정 wall-clock 주기가 아니라 response delivery 뒤의 고정 휴식이다. Round는 겹치지 않으며 실제 간격은 request 시간, delivery 시간, interval의 합이다. 100 ms 미만 interval은 100 ms로 올린다. `immediate`는 첫 round를 즉시 시작할지 interval 뒤에 시작할지 정한다. `max_rounds == 0`은 cancel 또는 stop까지 반복한다.

Transport error와 HTTP error status는 recovery 관측을 위해 heartbeat를 중단하지 않는다. Cancellation, round 소진, stop, invalid URL, unsupported scheme, invalid header 같은 영구 request error가 중단 조건이다. Retry 상한이나 adaptive backoff는 application이 response를 보고 flow를 cancel한 뒤 새 schedule을 시작해 구현한다.

## 취소

`cancel(ticket)`은 모르는 ticket이나 완료된 ticket에 대해 멱등적이다. Accepted one-shot request는 정확히 하나의 `http_error_kind::cancelled` 최종 response를 낸다. Active heartbeat를 취소하면 현재 response가 cancelled이자 final이 된다. Idle heartbeat를 취소해도 새 round가 없었던 상태에서 final cancelled response를 만든다. 한 round도 시작하지 않았다면 sequence는 0이다.

`cancel_all()`은 모든 active request와 idle heartbeat에 같은 규칙을 적용한다. Worker shutdown 전에 `logic_driver::cancel()`에서 호출하면 최종 종료가 빨라진다. Cancellation race에서는 request에 처음 기록된 terminal reason만 이기며, handle을 닫은 뒤 결과 하나를 publish한다.

## 정지와 파괴

`stop()`은 멱등적이며 concurrent caller 사이에서 직렬화된다. 먼저 admission을 닫고 heartbeat schedule을 제거하며 delivery를 비활성화한다. 이어 processing thread를 닫고 join하고, active WinHTTP request를 stopped로 취소하고, handle-close callback을 `stop_budget`까지 기다린 뒤 session을 닫는다.

`stop()` 반환 뒤에는 `deliver`가 다시 호출되지 않는다. Shutdown으로 종료된 request의 관측된 response는 `stopped`지만, internal channel에서 기다리던 completion은 process shutdown 중 비싼 parsing을 피하려고 폐기될 수 있다. 따라서 outstanding request마다 최종 callback을 보장하지 않는다.

WinHTTP는 request의 proxy 해석 RPC가 아직 binding 중일 때 handle이 닫히면 그 RPC를 취소하는데, 이 취소가 handle close callback 뒤에도 WinHTTP thread pool에서 이어지다 RPCRT4 안에서 접근 위반이나 `RPC_NT_INTERNAL_ERROR`(0xC0020043)로 process를 끝낼 수 있다. Session을 닫는 시점과는 관계없으며, `send` 직후의 `stop`만으로 들어간다. 같은 방식으로 handle을 닫는 `cancel`도 같은 경로에 들어갈 수 있다. 직접 연결 client는 위의 request별 proxy 지정으로 그 RPC 자체를 건너뛴다. System proxy를 쓰는 client에서는 같은 반복 부하로 재현되지 않았지만 PAC나 WPAD 환경에서는 확인하지 않았다.

WinHTTP가 budget 안에 모든 handle을 해제하지 않으면 작은 engine과 session allocation을 의도적으로 남긴다. Callback이 볼 수 있는 상태를 해제하면 use-after-free 위험이 있으므로 이미 종료 중인 process의 shutdown 시간을 제한하는 선택이다.

Destructor는 `stop()`을 호출한다. `deliver`가 참조하는 resource를 소유한 application은 그 resource보다 먼저 `stop()`을 명시적으로 호출해야 한다. 표준 host lifecycle에서는 `logic_driver::cancel()`에서 `cancel_all()`, `logic_driver::stop_workers()`에서 `stop()`을 호출한다.

## TLS와 trust boundary

HTTPS는 지원되는 환경에서 TLS 1.2 또는 1.3을 사용하고 WinHTTP certificate validation을 유지한다. Certificate error 무시와 HTTPS-to-HTTP redirect 허용 API는 없다. Proxy 사용은 configuration에서 명시하며, system proxy 비활성화는 통제된 loopback test에 유용하다. `use_system_proxy = false`는 session뿐 아니라 request handle마다 `WINHTTP_OPTION_PROXY`로 직접 연결을 지정한다. Session만 `NO_PROXY`로 열면 WinHTTP가 요청마다 process 밖 proxy 해석 RPC를 띄우기 때문이다.

## 검증

[`tests/http_media_type_tests.cpp`](../tests/http_media_type_tests.cpp)는 media parsing, classification, conservative sniffing을 검증한다. [`tests/http_body_tests.cpp`](../tests/http_body_tests.cpp)는 JSON 제한, charset 변환, HTML, image, raw byte 보존, parse 실패를 검증한다. [`tests/http_client_tests.cpp`](../tests/http_client_tests.cpp)는 [`tests/loopback_http_server.cpp`](../tests/loopback_http_server.cpp)를 사용해 method, header, body, redirect, 제한, timeout, cancellation, heartbeat sequencing, admission backpressure, callback thread, concurrent send, shutdown 보장을 반복 검증한다.
