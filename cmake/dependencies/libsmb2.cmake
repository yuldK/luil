include_guard(GLOBAL)

# libsmb2(LGPL-2.1)는 `smb_client`의 SMB 전송이다. Android에서 LUIL_ENABLE_SMB를 켠 구성에만 세운다
# (docs/smb-client-design.md).
#  - upstream의 CMakeLists는 들이지 않는다. 디렉터리 전체에 정의·include 경로를 걸고 `BUILD_SHARED_LIBS`
#    옵션을 캐시에 세워 luil의 target까지 번진다. 같은 소스 목록과 config.h를 여기서 세운다.
#  - **공유 라이브러리로 세운다.** LGPL-2.1의 "사용자가 라이브러리를 바꿔 끼울 수 있어야 한다"를 따로 선
#    `libsmb2.so`가 채운다. luil은 정적 라이브러리라 정적으로 섞으면 그 조건이 쓰는 앱 전체로 번진다.
#  - 고치지 않는다. 판은 submodule이 고정한다 (libsmb2-6.2).
#  - 제3자 C 코드라 luil의 경고 정책 밖이다 (`luil_options`를 걸지 않는다).
function(luil_find_libsmb2)
    set(root "${LUIL_LIBSMB2_ROOT}")
    if(NOT EXISTS "${root}/lib/libsmb2.c")
        message(FATAL_ERROR
            "The libsmb2 submodule is not initialized: ${root}\n"
            "Run: git submodule update --init third_party/libsmb2")
    endif()

    # upstream의 cmake/ConfigureChecks.cmake와 같은 검사다. 결과를 upstream의 config.h 틀에 넣는다.
    include(CheckIncludeFile)
    include(CheckStructHasMember)
    foreach(header_and_variable
        "arpa/inet.h|HAVE_ARPA_INET_H"
        "dlfcn.h|HAVE_DLFCN_H"
        "fcntl.h|HAVE_FCNTL_H"
        "inttypes.h|HAVE_INTTYPES_H"
        "netdb.h|HAVE_NETDB_H"
        "netinet/in.h|HAVE_NETINET_IN_H"
        "netinet/tcp.h|HAVE_NETINET_TCP_H"
        "poll.h|HAVE_POLL_H"
        "stdint.h|HAVE_STDINT_H"
        "stdio.h|HAVE_STDIO_H"
        "stdlib.h|HAVE_STDLIB_H"
        "strings.h|HAVE_STRINGS_H"
        "string.h|HAVE_STRING_H"
        "sys/ioctl.h|HAVE_SYS_IOCTL_H"
        "sys/poll.h|HAVE_SYS_POLL_H"
        "sys/socket.h|HAVE_SYS_SOCKET_H"
        "sys/stat.h|HAVE_SYS_STAT_H"
        "sys/types.h|HAVE_SYS_TYPES_H"
        "sys/uio.h|HAVE_SYS_UIO_H"
        "sys/_iovec.h|HAVE_SYS__IOVEC_H"
        "sys/time.h|HAVE_SYS_TIME_H"
        "sys/unistd.h|HAVE_SYS_UNISTD_H"
        "sys/errno.h|HAVE_SYS_ERRNO_H"
        "time.h|HAVE_TIME_H"
        "unistd.h|HAVE_UNISTD_H"
        "errno.h|HAVE_ERRNO_H"
        "stddef.h|STDC_HEADERS")
        string(REPLACE "|" ";" pair "${header_and_variable}")
        list(GET pair 0 header)
        list(GET pair 1 variable)
        check_include_file("${header}" ${variable})
    endforeach()
    check_struct_has_member("struct sockaddr" sa_len sys/socket.h HAVE_SOCKADDR_LEN)
    check_struct_has_member("struct sockaddr_storage" ss_family sys/socket.h HAVE_SOCKADDR_STORAGE)
    check_struct_has_member("struct linger" l_linger sys/socket.h HAVE_LINGER)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/libsmb2")
    configure_file("${root}/cmake/config.h.cmake" "${generated}/config.h")

    # upstream lib/CMakeLists.txt의 기본 갈래와 같은 목록이다. Kerberos(krb5-wrapper.c)는 넣지 않는다 — NTLM만 쓴다.
    set(sources
        aes.c aes128ccm.c alloc.c asn1-ber.c compat.c dcerpc.c dcerpc-lsa.c dcerpc-srvsvc.c errors.c hmac.c hmac-md5.c
        init.c libsmb2.c md4c.c md5.c ntlmssp.c pdu.c sha1.c sha224-256.c sha384-512.c smb2-cmd-close.c smb2-cmd-create.c
        smb2-cmd-echo.c smb2-cmd-error.c smb2-cmd-flush.c smb2-cmd-ioctl.c smb2-cmd-lock.c smb2-cmd-logoff.c
        smb2-cmd-negotiate.c smb2-cmd-notify-change.c smb2-cmd-oplock-break.c smb2-cmd-query-directory.c
        smb2-cmd-query-info.c smb2-cmd-read.c smb2-cmd-session-setup.c smb2-cmd-set-info.c smb2-cmd-tree-connect.c
        smb2-cmd-tree-disconnect.c smb2-cmd-write.c smb2-data-file-info.c smb2-data-filesystem-info.c
        smb2-data-security-descriptor.c smb2-data-reparse-point.c smb2-share-enum.c smb3-seal.c smb2-signing.c socket.c
        spnego-wrapper.c sync.c timestamps.c unicode.c usha.c)
    list(TRANSFORM sources PREPEND "${root}/lib/")

    add_library(luil_smb2 SHARED ${sources})
    add_library(luil::smb2 ALIAS luil_smb2)
    # 파일 이름은 upstream과 같은 libsmb2.so다. 고지와 사용자가 바꿔 끼울 파일이 같은 이름을 가리킨다.
    set_target_properties(luil_smb2 PROPERTIES OUTPUT_NAME smb2 C_STANDARD 99 C_EXTENSIONS ON)
    target_include_directories(luil_smb2
        PRIVATE
            "${generated}"
            "${root}/include/smb2"
            "${root}/lib")
    target_include_directories(luil_smb2 SYSTEM PUBLIC "${root}/include")
    target_compile_definitions(luil_smb2
        PRIVATE
            HAVE_CONFIG_H
            _FILE_OFFSET_BITS=64
            "_U_=__attribute__((unused))")
    # upstream 코드의 경고는 우리가 고칠 자리가 아니다.
    target_compile_options(luil_smb2 PRIVATE -w)
endfunction()
