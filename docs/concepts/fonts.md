# 글꼴 목록과 기본 UI 글꼴

`luil/text/fonts.h` 또는 `luil/luil.h`를 포함하면 공개 API를 사용할 수 있다.

```cpp
const auto families = luil::installed_font_families();
luil::set_ui_typeface_families({ u8"Pretendard", u8"Malgun Gothic", u8"Segoe UI" });
```

목록은 DirectWrite에서 읽은 UTF-8 가족 이름이며 정렬과 중복 제거를 거친다. 조회에 실패하면 빈 목록을 반환한다. 기본 UI 글꼴은 앞에서부터 설치된 가족을 선택하며, 빈 목록이나 사용할 수 없는 이름만 지정하면 Segoe UI를 사용한다. 앱 시작 전에 설정한다. 호출 시 내부 글꼴 캐시를 비우지만 이미 만들어진 화면과 글꼴 객체를 갱신하지는 않는다.

드래그의 기본 대상 강조를 앱에서 직접 그리려면 payload의 `suppress_drop_highlight`를 `true`로 설정한다. 대상 테두리와 목록·탭의 drop 배경이 사라지고 hit test와 drop 액션은 유지된다. ghost는 별도의 `custom_visual`로 제어한다.
