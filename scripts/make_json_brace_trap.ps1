[CmdletBinding()]
param(
    [string]$output = (Join-Path $PSScriptRoot '../build/json-brace-trap/include'),
    [string]$source = (Join-Path $PSScriptRoot '../third_party/nlohmann_json/single_include')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# nlohmann/json의 중괄호 함정을 실행 중에 잡는 진단용 json.hpp 사본을 만든다.
#  - `json x { y }`·`auto x { j }`·`return { j };`·`f({ j })`는 MSVC에서 복사지만 clang·gcc에서는
#    initializer_list 생성자가 이겨 원소 하나짜리 배열 `[y]`가 된다. 형이 `auto`로 숨으면 grep이 못 본다.
#  - 사본의 json_ref는 "이미 있는 json 값에서 왔는가"를 기억한다. 형 추론을 하는 initializer_list
#    생성자가 그런 원소 **하나만** 받으면 backtrace를 찍고 멈춘다. `json::array({ x })`·`json::object(...)`는
#    형 추론을 끄고 들어오고, `{ { "k", x } }`는 바깥 원소가 중첩 목록이라 걸리지 않는다.
#  - clang·gcc에서만 뜻이 있다. MSVC는 그 자리를 복사로 골라 생성자에 오지 않는다.
#  - 쓰는 법: 출력 폴더를 `CMAKE_CXX_FLAGS`의 `-I`로 넣어 따로 구성한 빌드에서 시험을 돌린다.
#    원본은 SYSTEM(`-isystem`) 포함이라 `-I`인 사본이 먼저 찾힌다. 환경 변수
#    `LUIL_JSON_BRACE_TRAP_WARN`이 있으면 멈추지 않고 찍기만 해 한 번에 다 모은다.
#  - 저장소에는 원본만 둔다. 판을 올려 고칠 자리가 달라지면 이 script가 멈추고 알린다.

$source_header = Join-Path $source 'nlohmann/json.hpp'
$source_forward = Join-Path $source 'nlohmann/json_fwd.hpp'
if (-not (Test-Path -LiteralPath $source_header)) {
    Write-Host "nlohmann/json.hpp not found under: $source" -ForegroundColor Red
    exit 1
}
# 이 script는 CRLF라 here-string도 CRLF다. 원본의 줄 끝과 상관없이 맞추도록 둘 다 LF로 본다.
$text = [System.IO.File]::ReadAllText($source_header).Replace("`r`n", "`n")

function Set-Patch {
    param(
        [Parameter(Mandatory)]
        [string]$anchor,

        [Parameter(Mandatory)]
        [string]$replacement
    )

    $anchor = $anchor.Replace("`r`n", "`n")
    $replacement = $replacement.Replace("`r`n", "`n")
    $count = ([regex]::Matches($script:text, [regex]::Escape($anchor))).Count
    if ($count -ne 1) {
        Write-Host "The nlohmann/json header changed; this anchor was found $count time(s):`n$anchor" -ForegroundColor Red
        exit 1
    }
    $script:text = $script:text.Replace($anchor, $replacement)
}

Set-Patch -anchor @'
NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{

template<typename BasicJsonType>
class json_ref
'@ -replacement @'
#include <cstdio>
#include <cstdlib>
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define LUIL_JSON_BRACE_TRAP_BACKTRACE 1
#endif
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{
inline void luil_json_brace_trap()
{
    std::fprintf(stderr, "LUIL_JSON_BRACE_TRAP: a braced list with one existing json value became a one-element array (use `=`).\n");
#if defined(LUIL_JSON_BRACE_TRAP_BACKTRACE)
    void* frames[32] {};
    const int count = ::backtrace(frames, 32);
    ::backtrace_symbols_fd(frames, count, 2);
#endif
    if (std::getenv("LUIL_JSON_BRACE_TRAP_WARN") == nullptr)
    {
        std::abort();
    }
}
}  // namespace detail
NLOHMANN_JSON_NAMESPACE_END

NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{

template<typename BasicJsonType>
class json_ref
'@

Set-Patch -anchor @'
    json_ref(value_type&& value)
        : owned_value(std::move(value))
    {}

    json_ref(const value_type& value)
        : value_ref(&value)
    {}
'@ -replacement @'
    json_ref(value_type&& value)
        : owned_value(std::move(value))
    {
        luil_from_value_ = true;
    }

    json_ref(const value_type& value)
        : value_ref(&value)
    {
        luil_from_value_ = true;
    }
'@

# const가 아닌 json lvalue(`json&`)는 위의 `const value_type&`가 아니라 이 가변 생성자로 온다.
Set-Patch -anchor @'
    json_ref(Args && ... args)
        : owned_value(std::forward<Args>(args)...)
    {}
'@ -replacement @'
    json_ref(Args && ... args)
        : owned_value(std::forward<Args>(args)...)
    {
        luil_from_value_ = sizeof...(Args) == 1 && (std::is_base_of<value_type, typename std::remove_cv<typename std::remove_reference<Args>::type>::type>::value && ...);
    }

    bool luil_from_value() const noexcept
    {
        return luil_from_value_;
    }
'@

Set-Patch -anchor @'
    mutable value_type owned_value = nullptr;
    value_type const* value_ref = nullptr;
};
'@ -replacement @'
    mutable value_type owned_value = nullptr;
    value_type const* value_ref = nullptr;
    bool luil_from_value_ = false;
};
'@

Set-Patch -anchor @'
               value_t manual_type = value_t::array)
    {
        // check if each element is an array with two elements whose first
'@ -replacement @'
               value_t manual_type = value_t::array)
    {
        if (type_deduction && init.size() == 1 && init.begin()->luil_from_value())
        {
            detail::luil_json_brace_trap();
        }

        // check if each element is an array with two elements whose first
'@

# 경고는 원본처럼 접는다 (원본은 SYSTEM 포함이고 사본은 `-I`로 들어온다).
$text = "#pragma GCC system_header`n" + $text

$target_directory = Join-Path $output 'nlohmann'
[System.IO.Directory]::CreateDirectory($target_directory) | Out-Null
[System.IO.File]::WriteAllText((Join-Path $target_directory 'json.hpp'), $text, [System.Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $source_forward -Destination (Join-Path $target_directory 'json_fwd.hpp') -Force
Write-Host "Wrote $((Resolve-Path -LiteralPath (Join-Path $target_directory 'json.hpp')).Path)"
exit 0
