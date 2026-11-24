#include "macos/input_script.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// 대본의 `drop`이 view에 건네는 끌기다. view가 묻는 두 가지(자리·붙여넣기 판)만 답한다.
@interface LuilScriptDragging : NSObject
@property(nonatomic, strong) NSPasteboard* board;
@property(nonatomic, assign) NSPoint location;
@end

@implementation LuilScriptDragging

- (NSPasteboard*)draggingPasteboard
{
    return self.board;
}

- (NSPoint)draggingLocation
{
    return self.location;
}

@end

namespace {
    struct script_state
    {
        std::vector<std::string> lines {};
        std::size_t next { 0 };
        NSWindow* __weak window { nil };
        NSView* __weak view { nil };
        // 주 창의 view다. 다른 창의 view를 묻는 창구다.
        NSView* __weak main_view { nil };
    };

    [[nodiscard]] NSPoint window_point(NSView* const view, const double x, const double y)
    {
        // 대본의 좌표는 view(뒤집힌 좌표계)의 점이다. 이벤트는 창 좌표(왼쪽 아래 원점)로 싣는다.
        return [view convertPoint:NSMakePoint(x, y) toView:nil];
    }

    void post_mouse(NSWindow* const window, NSView* const view, const NSEventType type, const double x, const double y, const NSInteger clicks)
    {
        NSEvent* const event { [NSEvent mouseEventWithType:type
                                                  location:window_point(view, x, y)
                                             modifierFlags:0
                                                 timestamp:[[NSProcessInfo processInfo] systemUptime]
                                              windowNumber:window.windowNumber
                                                   context:nil
                                               eventNumber:0
                                                clickCount:clicks
                                                  pressure:type == NSEventTypeLeftMouseUp || type == NSEventTypeRightMouseUp ? 0.0f : 1.0f] };
        [NSApp postEvent:event atStart:NO];
    }

    void post_key(NSWindow* const owner, const unsigned short code, NSString* const characters, NSString* const base, const NSEventModifierFlags flags)
    {
        // 창에 sheet(파일 패널 등)가 붙어 있으면 키는 그 sheet로 간다.
        NSWindow* const window { owner.attachedSheet != nil ? owner.attachedSheet : owner };
        for (const NSEventType type : { NSEventTypeKeyDown, NSEventTypeKeyUp })
        {
            NSEvent* const event { [NSEvent keyEventWithType:type
                                                    location:NSZeroPoint
                                               modifierFlags:flags
                                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                                windowNumber:window.windowNumber
                                                     context:nil
                                                  characters:characters
                                 charactersIgnoringModifiers:base
                                                   isARepeat:NO
                                                     keyCode:code] };
            [NSApp postEvent:event atStart:NO];
        }
    }

    // 가상 키의 글자다. 대본이 쓰는 키만 둔다.
    [[nodiscard]] NSString* key_characters(const unsigned short code)
    {
        switch (code)
        {
        case 0x24:
            return @"\r";
        case 0x30:
            return @"\t";
        case 0x31:
            return @" ";
        case 0x33:
            return @"\x7f";
        case 0x35:
            return @"\x1b";
        case 0x7B:
            return [NSString stringWithFormat:@"%C", static_cast<unichar>(NSLeftArrowFunctionKey)];
        case 0x7C:
            return [NSString stringWithFormat:@"%C", static_cast<unichar>(NSRightArrowFunctionKey)];
        case 0x7D:
            return [NSString stringWithFormat:@"%C", static_cast<unichar>(NSDownArrowFunctionKey)];
        case 0x7E:
            return [NSString stringWithFormat:@"%C", static_cast<unichar>(NSUpArrowFunctionKey)];
        case 0x08:
            return @"c";
        case 0x09:
            return @"v";
        case 0x00:
            return @"a";
        case 0x06:
            return @"z";
        case 0x07:
            return @"x";
        default:
            return @"";
        }
    }

    // 미국 배열의 글자 하나를 가상 키로 옮긴다. 대문자는 Shift를 단다.
    [[nodiscard]] bool key_for_character(const unichar character, unsigned short& code, bool& shift)
    {
        static const char* const lower { "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm." };
        shift = false;
        unichar value { character };
        if (value >= 'A' && value <= 'Z')
        {
            shift = true;
            value = static_cast<unichar>(value - 'A' + 'a');
        }
        for (unsigned short index { 0 }; index < 48; ++index)
            if (lower[index] != '\0' && static_cast<unichar>(lower[index]) == value)
            {
                code = index;
                return true;
            }
        if (value == ' ')
        {
            code = 0x31;
            return true;
        }
        return false;
    }

    // 글자 키의 소문자다. 글자 키가 아니면 비었다.
    [[nodiscard]] NSString* character_of_key(const unsigned short code)
    {
        static const char* const lower { "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm." };
        if (code >= 48 || lower[code] == '\0')
            return @"";
        const unichar value { static_cast<unichar>(lower[code]) };
        return [NSString stringWithCharacters:&value length:1];
    }

    void run_line(script_state& state, const std::string& line, double& delay)
    {
        // 겨누던 창이 사라졌으면(보조 창을 닫았다) 주 창으로 돌아온다.
        if (state.view == nil || state.window == nil)
        {
            state.view = state.main_view;
            state.window = state.main_view.window;
        }
        NSWindow* const window { state.window };
        NSView* const view { state.view };
        if (window == nil || view == nil)
            return;
        std::istringstream words { line };
        std::string command {};
        words >> command;
        if (command == "wait")
        {
            words >> delay;
            delay /= 1000.0;
        }
        else if (command == "move")
        {
            double x { 0 };
            double y { 0 };
            words >> x >> y;
            post_mouse(window, view, NSEventTypeMouseMoved, x, y, 0);
        }
        else if (command == "click")
        {
            double x { 0 };
            double y { 0 };
            std::string button {};
            words >> x >> y >> button;
            const bool right { button == "right" };
            post_mouse(window, view, NSEventTypeMouseMoved, x, y, 0);
            post_mouse(window, view, right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown, x, y, 1);
            post_mouse(window, view, right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp, x, y, 1);
        }
        else if (command == "drag")
        {
            double x1 { 0 };
            double y1 { 0 };
            double x2 { 0 };
            double y2 { 0 };
            words >> x1 >> y1 >> x2 >> y2;
            post_mouse(window, view, NSEventTypeLeftMouseDown, x1, y1, 1);
            for (int step { 1 }; step <= 8; ++step)
            {
                const double t { step / 8.0 };
                post_mouse(window, view, NSEventTypeLeftMouseDragged, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, 0);
            }
            post_mouse(window, view, NSEventTypeLeftMouseUp, x2, y2, 1);
        }
        else if (command == "scroll")
        {
            double x { 0 };
            double y { 0 };
            int lines { 0 };
            words >> x >> y >> lines;
            post_mouse(window, view, NSEventTypeMouseMoved, x, y, 0);
            CGEventRef const wheel { CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, lines) };
            if (wheel != nullptr)
            {
                const NSPoint screen { [window convertPointToScreen:window_point(view, x, y)] };
                CGEventSetLocation(wheel, CGPointMake(screen.x, NSMaxY(window.screen.frame) - screen.y));
                if (NSEvent* const event { [NSEvent eventWithCGEvent:wheel] }; event != nil)
                    [view scrollWheel:event];
                CFRelease(wheel);
            }
        }
        else if (command == "key")
        {
            unsigned code { 0 };
            words >> std::hex >> code >> std::dec;
            NSEventModifierFlags flags { 0 };
            for (std::string modifier {}; words >> modifier;)
            {
                if (modifier == "cmd")
                    flags |= NSEventModifierFlagCommand;
                else if (modifier == "shift")
                    flags |= NSEventModifierFlagShift;
                else if (modifier == "option")
                    flags |= NSEventModifierFlagOption;
                else if (modifier == "control")
                    flags |= NSEventModifierFlagControl;
            }
            NSString* characters { key_characters(static_cast<unsigned short>(code)) };
            // 글자 키면 그 글자를 싣는다 (Command+Shift+G 같은 단축키가 글자로 맞춰진다).
            if (characters.length == 0)
                characters = character_of_key(static_cast<unsigned short>(code));
            post_key(window, static_cast<unsigned short>(code), characters, characters, flags);
        }
        else if (command == "type")
        {
            std::string rest {};
            std::getline(words, rest);
            NSString* const text { [[NSString stringWithUTF8String:rest.c_str()] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] };
            for (NSUInteger index { 0 }; index < text.length; ++index)
            {
                const unichar character { [text characterAtIndex:index] };
                unsigned short code { 0 };
                bool shift { false };
                if (key_for_character(character, code, shift) == false)
                    continue;
                NSString* const value { [NSString stringWithCharacters:&character length:1] };
                post_key(window, code, value, [value lowercaseString], shift ? NSEventModifierFlagShift : 0);
            }
        }
        else if (command == "marked" || command == "commit")
        {
            std::string rest {};
            std::getline(words, rest);
            NSString* const text { [[NSString stringWithUTF8String:rest.c_str()] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] };
            id<NSTextInputClient> const client { (id<NSTextInputClient>)view };
            if (command == "marked")
                [client setMarkedText:text selectedRange:NSMakeRange(text.length, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
            else
                [client insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
        }
        else if (command == "snapshot")
        {
            std::string file {};
            std::string surface {};
            words >> file >> surface;
            NSString* const path { [NSString stringWithUTF8String:file.c_str()] };
            if (surface.empty() == false && [view respondsToSelector:@selector(luilSnapshot:surface:)])
                [view performSelector:@selector(luilSnapshot:surface:) withObject:path withObject:[NSString stringWithUTF8String:surface.c_str()]];
            else if ([view respondsToSelector:@selector(luilSnapshot:)])
                [view performSelector:@selector(luilSnapshot:) withObject:path];
        }
        else if (command == "ax")
        {
            // 접근성 계층을 적는다 (역할·이름·값, 깊이 4까지). 앱 안에서 NSAccessibility 질문으로 읽으므로 권한이 없어도 된다.
            const auto dump = [](const auto& self, id const element, const int depth) -> void {
                if (depth > 4 || element == nil)
                    return;
                NSString* const role { [element respondsToSelector:@selector(accessibilityRole)] ? [element accessibilityRole] : @"?" };
                NSString* const label { [element respondsToSelector:@selector(accessibilityLabel)] ? [element accessibilityLabel] : nil };
                id const value { [element respondsToSelector:@selector(accessibilityValue)] ? [element accessibilityValue] : nil };
                // 행이면 차례와 골라짐을 덧붙인다.
                const bool row { [role isEqualToString:NSAccessibilityRowRole] };
                NSString* const extra { row ? [NSString stringWithFormat:@" [row %ld%s]", static_cast<long>([element accessibilityIndex]),
                                                   [element isAccessibilitySelected] ? " selected" : ""]
                                            : @"" };
                std::fprintf(stderr, "luil ax %*s%s \"%s\" %s%s\n", depth * 2, "", role.UTF8String ?: "", label.UTF8String ?: "",
                    value != nil ? [[value description] UTF8String] : "", extra.UTF8String);
                if ([element respondsToSelector:@selector(accessibilityChildren)])
                    for (id const child in [element accessibilityChildren])
                        self(self, child, depth + 1);
            };
            for (id const child in [window accessibilityChildren])
                dump(dump, child, 0);
            id const focused { [window accessibilityFocusedUIElement] };
            std::fprintf(stderr, "luil ax focused: %s \"%s\"\n", [[focused accessibilityRole] UTF8String] ?: "(none)", [[focused accessibilityLabel] UTF8String] ?: "");
        }
        else if (command == "capture" || command == "record")
        {
            // capture <경로> — 지금 창을 WebP로 남긴다. record <경로> — 녹화를 시작한다. record만 쓰면 멈춘다.
            std::string file {};
            words >> file;
            NSString* const path { [NSString stringWithUTF8String:file.c_str()] };
            if (command == "capture" && [view respondsToSelector:@selector(luilCapture:)])
                [view performSelector:@selector(luilCapture:) withObject:path];
            else if (command == "record" && [view respondsToSelector:@selector(luilRecord:)])
                [view performSelector:@selector(luilRecord:) withObject:path];
        }
        else if (command == "window")
        {
            std::string id {};
            words >> id;
            NSView* target { state.main_view };
            if (id != "main" && [state.main_view respondsToSelector:@selector(luilViewForSurface:)])
                target = [state.main_view performSelector:@selector(luilViewForSurface:) withObject:[NSString stringWithUTF8String:id.c_str()]];
            if (target != nil)
            {
                state.view = target;
                state.window = target.window;
            }
        }
        else if (command == "drop")
        {
            // 파일 하나를 그 자리에 끌어 와 놓는다. view의 끌어 놓기 길(`draggingEntered:` → `performDragOperation:`)을 밟는다.
            double x { 0 };
            double y { 0 };
            std::string rest {};
            words >> x >> y;
            std::getline(words, rest);
            NSString* const path { [[NSString stringWithUTF8String:rest.c_str()] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] };
            NSPasteboard* const board { [NSPasteboard pasteboardWithUniqueName] };
            [board clearContents];
            [board writeObjects:@[ [NSURL fileURLWithPath:path] ]];
            LuilScriptDragging* const info { [LuilScriptDragging new] };
            info.board = board;
            info.location = window_point(view, x, y);
            id<NSDraggingDestination> const target { (id<NSDraggingDestination>)view };
            const NSDragOperation entered { [target draggingEntered:(id<NSDraggingInfo>)info] };
            const NSDragOperation updated { [target draggingUpdated:(id<NSDraggingInfo>)info] };
            const BOOL dropped { updated != NSDragOperationNone && [target performDragOperation:(id<NSDraggingInfo>)info] };
            if (updated == NSDragOperationNone)
                [target draggingExited:(id<NSDraggingInfo>)info];
            std::fprintf(stderr, "luil script: drop entered=%lu dropped=%d\n", static_cast<unsigned long>(entered), dropped ? 1 : 0);
            [board releaseGlobally];
        }
        else if (command == "sheet")
        {
            // 창에 붙은 sheet를 다룬다. 낱말 없이는 그 종류만 적는다.
            //   sheet cancel | name <저장 이름> | folder <경로>
            std::string action {};
            std::string argument {};
            words >> action;
            std::getline(words, argument);
            NSWindow* const sheet { window.attachedSheet };
            NSString* const value { [[NSString stringWithUTF8String:argument.c_str()] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] };
            std::fprintf(stderr, "luil script: sheet %s\n", sheet != nil ? NSStringFromClass([sheet class]).UTF8String : "(none)");
            if ([sheet isKindOfClass:[NSSavePanel class]])
            {
                NSSavePanel* const panel { (NSSavePanel*)sheet };
                // 패널은 다른 프로세스가 그린다 (`ok:`가 없다). 고르기는 키(Command+Shift+G로 경로)로 하고, 여기서는 그만두기만 한다.
                if (action == "cancel")
                    [panel cancel:nil];
                else if (action == "name")
                    panel.nameFieldStringValue = value;
                else if (action == "folder")
                    panel.directoryURL = [NSURL fileURLWithPath:value isDirectory:YES];
            }
        }
        else if (command == "quit")
            [window performClose:nil];
    }

    void schedule(const std::shared_ptr<script_state>& state, const double delay)
    {
        // 블록은 C++ 참조를 참조로 잡는다. 값으로 잡히게 지역 사본을 둔다.
        const std::shared_ptr<script_state> held { state };
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(delay * 1e9)), dispatch_get_main_queue(), ^{
          if (held->next >= held->lines.size())
              return;
          double next_delay { 0.05 };
          run_line(*held, held->lines[held->next++], next_delay);
          schedule(held, next_delay);
        });
    }
}

void luil_run_input_script(NSWindow* const window, NSView* const view)
{
    const char* const path { std::getenv("LUIL_MACOS_INPUT_SCRIPT") };
    if (path == nullptr || *path == '\0')
        return;
    std::ifstream file { path };
    if (file.is_open() == false)
        return;
    auto state { std::make_shared<script_state>() };
    for (std::string line {}; std::getline(file, line);)
        if (line.empty() == false && line.front() != '#')
            state->lines.push_back(line);
    state->window = window;
    state->view = view;
    state->main_view = view;
    schedule(state, 0.5);
}
