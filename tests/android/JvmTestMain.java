package luil.testing;

import java.util.Arrays;

// adb 셸의 app_process가 띄우는 test 진입점이다 (cmake/android/adb_run.cmake의 JVM_DEX).
//
// 첫 인자는 test 공유 라이브러리의 절대 경로이고 나머지는 Catch2에 그대로 간다. JNI로 Java를
// 부르는 test는 JVM 안에서만 돌 수 있는데, 셸에서 띄운 실행 파일에는 JVM이 없다.
public final class JvmTestMain {
    private static native int run(String[] arguments);

    public static void main(String[] arguments) {
        System.load(arguments[0]);
        int code = run(Arrays.copyOfRange(arguments, 1, arguments.length));
        System.out.flush();
        System.exit(code);
    }
}
