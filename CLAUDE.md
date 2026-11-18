# WimSmith — 개발 노트

[English](CLAUDE.en.md)

[wimlib](https://wimlib.net)으로 볼륨이나 폴더를 WIM 이미지로 백업하고 복구하는 작은 Windows 도구입니다. 이 파일은 프로그램과 그 소스를 다루는 사람을 위한 설명입니다.

나머지 문서는 프로그램을 쓰는 사람을 위한 것이며 릴리스에 함께 들어갑니다. 기능이 바뀌면 함께 고치세요.

| 파일 | 용도 |
|---|---|
| `README.md`, `README.en.md` | 릴리스: 무엇인지, 무엇을 받는지, 시작하는 방법, 라이선스 |
| `MANUAL.md`, `MANUAL.en.md` | 사용 설명서: 모든 탭, 자주 하는 작업, 설정, 문제 해결 |

모든 문서는 한국어판이 `<이름>.md`(기본판, GitHub에 먼저 보임), 영어판이 `<이름>.en.md`이며, 서로 첫 줄에서 링크합니다. 둘을 함께 고치세요. `LICENSE`는 영어만 둡니다(MIT 원문만 법적 효력이 있음).

## 기능

- **백업**: 볼륨이나 폴더를 새 WIM 파일로, 또는 기존 파일의 새 이미지로 저장합니다(LZX / XPRESS / LZMS, 스레드 수). **VSS 스냅숏**을 켜면 원본을 볼륨 섀도 복사본에서 읽습니다(wimlib의 snapshot 옵션). 사용 중인 파일은 그 순간의 상태로 저장되고, 실행 중인 Windows 드라이브도 저장할 수 있습니다(이때 WIM 파일은 다른 드라이브에 있어야 함). 스냅숏 없이는 실행 중인 Windows 드라이브를 저장할 수 없습니다. **빠른 백업**("새 이미지 추가"와 함께, 작업 키 `update_of = 1`): 마지막 이미지를 기준으로 `wimlib_reference_template_image`를 써서 크기와 시각이 같은 파일은 다시 읽지 않습니다(wimcapture `--update-of`). **제외**(키 `exclude_count`, `x1`..`xn`): helper가 캡처 구성 파일(BOM 있는 UTF-16, wimlib의 Windows 기본값에 사용자 경로를 더한 것)을 작업 폴더에 쓰고 `WIMLIB_ADD_FLAG_WINCONFIG` 대신 넘깁니다. 목록은 ini의 `backup.exclude`(`|`로 구분)이고, 권장 목록은 `tab_backup.cpp`에 있습니다.
- **오른쪽 클릭 메뉴로 압축 풀기**(`.wim` / `.esd` / `.swm` 메뉴의 항목 `1_extract` / `2_extract_each`, `assoc::MenuItem::args` = `--extract one|each`, 메뉴와 이 항목들에 `MultiSelectModel = Player`. `UpdateMenuLanguage`는 첫 항목을 `xw.menu_one`과 비교): `src\extractwim.cpp`. main.cpp에서 메인 창보다 먼저, 권한 상승 없이, 이 프로세스 안에서 실행됩니다(`wl::Load`, 스레드, `ExtractNoAcls | ExtractReplaceInvalidNames`로 `wimlib_extract_image`, 작은 창에 진행률과 [취소]). 한 번의 클릭으로 들어온 파일을 모으고(`gather::Forward("Extract.<mode>")`, 아래 참고) 1초 동안 더 들어오지 않으면 시작합니다. `one`: 모든 파일을 새 폴더 `<첫 파일의 폴더>\<이름>`(있으면 `(2)`, `(3)`, …)에. `each`: 파일마다 하나. 이미지가 하나면 그 폴더에, 여럿이면 이미지마다 하위 폴더 `<번호> <이름>`. `.swm` 조각은 그 세트의 첫 조각으로 바뀌어 한 번만 풉니다(`SwmParts`).
- **폴더로 WIM 만들기**(폴더의 오른쪽 클릭 메뉴, [설정] "st.menu_folders": `assoc::RegisterFolderMenu`, HKCU `Software\Classes\Directory\shell\WimSmith`에 `SubCommands`, 항목 `1_one` / `2_each` / `3_open`(문구 `mw.menu_one` / `_each` / `_open`, mainwin.cpp의 `FolderMenu()`), `MultiSelectModel = Player`. 명령 `--make-wim <one|each|open> "%1"`. 언어나 항목 수가 다르면 시작할 때 다시 만듦, `UpdateMenuLanguage`): `src\makewim.cpp`. main.cpp에서 메인 창보다 먼저 실행됩니다. 탐색기는 고른 폴더마다 프로세스를 하나씩 시작합니다. `MakeWimForward`(`src\gather.*`, 압축 풀기와 공유)가 이름 있는 뮤텍스 `Local\WimSmith.MakeWim.<mode>`를 만들거나(첫 프로세스), 첫 프로세스의 메시지 전용 창 `WimSmith.MakeWim.<mode>.Receiver`에 `WM_COPYDATA`로 폴더를 보내고(약 10초 동안 다시 시도, 묻거나 실행 중이면 거절됨) 끝납니다. 창: 폴더([추가...] / [빼기]), *WIM 파일 하나(폴더마다 이미지)* 또는 *폴더마다 WIM 파일*(한 폴더에 `<폴더 이름>.wim`), 압축, 스레드, VSS 스냅숏, 검증, `--check`. `one` / `each`는 방식을 미리 고르고, 1초 동안 폴더가 더 들어오지 않으면 확인 없이 스스로 [시작]을 누릅니다(타이머 2). `open`은 [시작]을 기다립니다. [시작]은 권한 상승된 helper를 직접 실행하고(`%TEMP%\WimSmith_job\<시각>-<pid>`의 job.ini, `source_count = n`, `s<k>` / `w<k>` / `n<k>`, `append = 1`의 `capture`) 진행률을 보여 줍니다(`App::PhaseText`, 단계 `source <k> <n>`). helper는 서로 다른 `w<k>`마다 파일 하나를 쓰고(그 이미지를 모두 한 번에 쓰기, `CaptureFile`), 기존 파일에는 덧붙이며, 쓰인 이미지 이름은 "이름 (2)"로 바꾸고(`UniqueName`), 원본 안에 있는 WIM 파일은 거절합니다.
- **볼륨에 복구**: 두 번 확인한 뒤 볼륨을 포맷하고(NTFS) 그 루트에 이미지 하나를 적용합니다. Windows 드라이브, WimSmith의 드라이브, WIM 파일의 드라이브는 목록에 나오지 않습니다. 파티션을 다 읽으면(`LoadPartitions`) 대상 목록의 각 볼륨 끝에 디스크 형식과 번호가 붙습니다(`TargetText`, 문구 `rs.target_style`, 예: `· GPT 디스크 0`. `ShowTargetStyles`는 선택을 그대로 두고 변경 이벤트를 일으키지 않음). **느린 연결 경고**(문구는 `SlowLinkText(경로)`, app.cpp. 복구 탭 `CheckSource`와 백업 탭 `CheckTarget`은 `App::CheckLink`로 백그라운드에서: 복구는 이미지를 읽은 뒤, 백업은 [찾아보기]로 고른 뒤나 대상으로 넘어왔을 때. 폴더로 WIM 만들기 창은 `App`이 없고 스스로 시작하기도 하므로 [시작] 안에서 바로 확인해 경고한 뒤 진행, 저장 위치마다 한 번 `linkChecked`): 네트워크 경로(UNC나 네트워크 드라이브, `src\net.*`)면 서버로 가는 인터페이스의 받기 링크 속도가 100 Mbit/s 이하일 때 `rs.net_slow`(어댑터, 속도, 서버). 로컬 드라이브 문자면 그 USB 연결(`src\usb.*`)이 USB 2.0 이하(Low / Full / High speed)이고 그 포트나 PC의 다른 포트가 USB 3.0 이상을 지원할 때 경고합니다(포트가 USB 3.0이면 `rs.usb_slow_device`: 장치나 케이블이 2.0, 아니면 `rs.usb_slow_port`: 다른 포트로). 사용자가 고르거나 [이미지 읽기]를 누른 경우(`show`)만 창을 띄우고, 그 밖에는 로그에만 남깁니다. 파일마다 한 번(`srcWarned` / `srcLogged`, 백업은 `tgtWarned` / `tgtLogged`). 문구는 복구와 백업에 함께 맞게 "읽고 씁니다"입니다. **압축**(Compact OS, 키 `compact = 1..4`): `WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K / 8K / 16K / LZX`, 폴더에도 적용. 실행 중인 Windows가 10 이상일 때만(`RtlGetVersion`) 쓰이며, 이때는 크기 검사를 건너뜁니다.
- **폴더에 복구**: 포맷 없이 빈 폴더에 이미지를 풉니다.
- **디스크(파티션)**: 두 번 확인한 뒤(디스크 번호를 직접 입력) 디스크 전체를 지우고, GPT나 MBR로 초기화하고, 파티션을 만듭니다. *Windows용*: GPT = EFI 시스템(FAT32, 260 MB) + MSR(16 MB, 뺄 수 있음) + Windows(NTFS). MBR = 시스템 예약(NTFS, 500 MB, 활성) + Windows. Windows 뒤에 복구 파티션을 더할 수 있습니다. *Windows + 데이터*: 같은 구성에 끝에 데이터 파티션(NTFS / exFAT / FAT32). Windows는 남은 공간의 %를, 데이터 파티션은 나머지를 받습니다. *데이터 파티션 하나*: 디스크 전체에 NTFS / exFAT / FAT32. 실행 중인 Windows의 디스크와 WimSmith의 디스크는 고를 수 없습니다. Windows가 있는 구성 뒤에는 복구 탭이 새 파티션을 대상으로 받고, 새 시스템 파티션에 UEFI(GPT)나 BIOS(MBR) 부트 파일을 씁니다.
- 복구와 내보내기는 나눈 WIM(`.swm`)도 받습니다. 아무 조각이나 고르면 나머지 조각은 같은 폴더에서 찾습니다(이름을 바꾸지 않은 모든 조각).
- **내보내기**: WIM / ESD 파일의 이미지 하나나 전부를 새 WIM 파일이나 기존 WIM 파일로 복사합니다(다른 압축, ESD -> WIM, 여러 파일을 하나로. 이름과 설명을 바꿀 수 있음). 부트 이미지 표시는 유지됩니다. 관리자 권한이 필요 없습니다.
- **나누기 / 합치기**: WIM 파일을 고른 크기(예: FAT32 USB용 4000 MB)의 `.swm` 조각으로 나누고, 조각을 다시 WIM 파일 하나로 합칩니다(아무 조각에서나 나머지를 찾음). solid 리소스가 있는 WIM 파일(ESD / LZMS solid)은 나눌 수 없으니 먼저 LZX로 내보내세요. 관리자 권한이 필요 없습니다.
- **정보**: WIM 파일의 헤더(이미지, 압축, 크기, 부트 이미지, GUID, 플래그)와 이미지마다의 정보(에디션, 아키텍처, 버전, 언어, 크기, 파일, 날짜). 클립보드로 복사하거나 이미지를 복구 / 내보내기 탭으로 넘길 수 있습니다. **[관리 ▾]**(helper 모드 `modify`, 키 `op`): `props`(키 `name` / `description` / `flags` / `boot`, 바뀐 것만. `wimlib_set_image_property`, `wimlib_set_wim_info` 부트 번호), `delete`(`wimlib_delete_image`), `optimize`(`WIMLIB_WRITE_FLAG_REBUILD`), 그 뒤 `wimlib_overwrite`. delete / optimize는 파일 전체를 다시 쓰므로, 파일 크기만큼 빈 공간이 없으면 거절됩니다. `admin = 1`일 때만 권한 상승합니다(GUI가 `NeedsAdminToWrite`일 때 설정). 그 뒤 탐색 탭이 파일을 다시 읽습니다(`BrowseTab::WimChanged`).
- **검증**: 정보 탭의 [검증]은 WIM / ESD / SWM 파일의 모든 데이터를 읽어 SHA-1 해시, 모든 이미지의 메타데이터, 무결성 테이블(있을 때)과 비교합니다(`wimlib_verify_wim`). 백업 탭의 "백업 후 WIM 파일 검증"은 백업 직후 같은 일을 합니다. 아무것도 쓰지 않습니다.
- **무결성(`--check`)**: 백업, 복구, 내보내기, 나누기 / 합치기 탭의 옵션이며 기본은 꺼짐이고 저장하지 않습니다. 작업 키 `check = 1`: 파일을 `WIMLIB_OPEN_FLAG_CHECK_INTEGRITY`로 열고(무결성 테이블이 있으면 비교. 복구는 포맷 전에 검사) `WIMLIB_WRITE_FLAG_CHECK_INTEGRITY`로 씁니다(백업, 내보내기, 나눈 조각 하나하나, 합친 파일).
- **탐색**: wimlib으로 읽은 이미지의 파일(`wimlib_iterate_dir_tree`, GUI 프로세스 안, 권한 상승 없음). 폴더 트리와 파일 목록, 고른 파일 / 폴더를 폴더에 풀기(ACL 없이 `wimlib_extract_paths`), 파일 열기. 파일 목록은 글자를 입력해 고를 수 있습니다(가상 목록이므로 `LVN_ODFINDITEMW`에 답함. 입력 검색은 `LVFI_SUBSTRING`으로 옴). 편집 명령([편집] 버튼, 오른쪽 클릭 메뉴, Del / F2 / F5): 새 폴더, 파일 / 폴더 추가, 이름 바꾸기, 삭제. `wimlib_update_image` + `wimlib_overwrite`로 합니다(제자리에서 덧붙여 씀. 추가한 파일은 ACL 없음). 나눈 파일이나 읽기 전용 파일에는 쓸 수 없습니다. 사용자가 쓸 수 없는 파일(C: 루트, Program Files)은 권한 상승된 helper로 갑니다(작업 모드 `update`, 키 c<n>_op / _a / _b / _flags. `wl::UpdateImage`가 양쪽의 명령을 만듦). `7z.exe`로 목록을 읽고 풀던 버전은 `backup\cpp-7zip-browse\`에 있습니다(git 태그 `before-wimlib-browse`). 7-Zip은 더 쓰지 않습니다([7-Zip으로 열기] 버튼과 다운로드는 없앴음).
- **드라이버(DISM)**: 복구 탭은 복구한 Windows(볼륨이나 폴더 대상)에 폴더의 드라이버를 추가할 수 있습니다. 적용 뒤, 부트 파일 전에 합니다. 작업 키 `drivers`(폴더)와 `dism`(dism.exe), helper `AddDrivers`: `dism /Image:<루트> /Add-Driver /Driver:<폴더> /Recurse /LogPath:<작업 폴더>\dism.log`(`DismArg`는 공백이 있는 경로를 따옴표로 감싸고, 닫는 따옴표 앞의 `\`를 뺌). DISM이 실패해도 부트 파일은 쓰고, 작업은 그 코드로 끝납니다. 어떤 DISM을 쓸지: [설정] `dism = system | folder`와 `dism.folder`(`DismExe()`: `System32\dism.exe`, 또는 `<폴더>\dism.exe`. 상대 경로는 exe 폴더 기준, 예: ADK의 DISM 폴더). [설정]의 [가져오기](`ImportSystemDism`, settings.cpp)는 `System32\Dism\*`를 `dism.exe`, `dismapi.dll`, `wimgapi.dll`, 그 `<언어>\*.mui`와 함께 `<exe>\dism`에 평평하게 복사하고(ADK DISM 폴더의 구성), `dism = folder`, `dism.folder = dism`으로 설정합니다([확인]으로 저장). 탭의 폴더는 `restore.drivers`로 저장하고, 체크박스는 저장하지 않습니다.
- **부트 파일**: 볼륨 복구 뒤 `bcdboot`로 고른 시스템 파티션에 UEFI 및/또는 BIOS 부트 파일을 쓸 수 있습니다(부트 관리자 언어, 기본 `ko-kr`). 드라이브 문자가 없는 파티션은 그동안만 문자를 받습니다. **기존 BCD 지우기**(체크박스 `chkBcdDelete`, 기본 꺼짐, 저장 안 함, 작업 키 `bcd_delete = 1`): helper의 `DeleteBcd`가 `bcdboot` 전에 펌웨어에 맞는 BCD 저장소(UEFI `\EFI\Microsoft\Boot\BCD`, BIOS `\Boot\BCD`, ALL은 둘 다)와 그 `.LOG*`를 지웁니다(숨김 / 시스템 속성을 먼저 풂). 없는 파일은 괜찮고, 지우지 못하면 `bcdboot` 없이 그 오류로 끝납니다. 확인 창에 `rs.confirm_bcd_delete` 줄이 붙습니다.
- **완료 후**: 아무것도 안 함 / 시스템 종료 / 다시 시작. 취소할 수 있는 카운트다운 뒤에 합니다. 10초, [설정]에서 1 - 60초(`after_seconds`, settings.cpp의 `AfterSeconds()`).
- **설정**(바닥줄의 [설정], `MainWindow::ShowSettings`): 테마. **.wim / .esd / .swm을 WimSmith로 열기**, 각각 따로(`src\assoc.*`: HKCU\Software\Classes 아래 ProgID `WimSmith.wim` / `WimSmith.esd` / `WimSmith.swm`, 권한 상승 없음. `DefaultIcon` = `"<exe>",-2 / -3 / -4`, `app.rc`의 파일 아이콘(`icons\wim.ico`, `esd.ico`, `swm.ico`, `backup\icon\make_file_icons.ps1`로 그림: 프로그램 아이콘의 레이어가 있는 문서와 확장자 띠, WIM 청록 / ESD 보라 / SWM 주황). `assoc::UpdateIcons`가 이전 버전의 연결에 시작할 때 다시 설정). 각 확장자의 이전 기본값은 값 `WimSmith.PreviousDefault`에 두었다가 체크를 끄면 돌려줍니다. Windows에서 고른 UserChoice가 우선하며, 그때는 *연결 프로그램*에서 WimSmith를 고르라고 알려 줍니다. WIM 파일을 열 탭(`open_tab = browse | info | restore | append | export | splitjoin`, 기본 browse. `append` = 그 파일을 대상으로 "새 이미지 추가"를 고른 백업 탭, `splitjoin` = 나누기, `.swm`이면 합치기). `.wim` / `.esd` / `.swm`의 **오른쪽 클릭 메뉴** "WimSmith >"(`assoc::RegisterMenu`: HKCU\Software\Classes\SystemFileAssociations\<ext>\shell\WimSmith에 `SubCommands`와 항목 `1_restore`..; 어떤 프로그램으로 열리든 보임. Windows 11: "추가 옵션 표시" 안. .esd는 이미지 추가 / 나누기 없음, .swm은 이미지 추가 없음. 언어가 다르면 시작할 때 다시 만듦, `UpdateMenuLanguage`). **복구: 경로**(`relative_paths = 1`, 기본 0, `SetRelativePaths`): 복구 탭은 마지막으로 읽은 WIM 파일을 `restore.wim`으로 저장합니다(`SetRestoreWim`. 시작할 때 백업의 `image_folder`보다 먼저 씀). 같은 드라이브면 exe 폴더 기준 상대 경로로 저장합니다(`PathRelativePathToW`. `RestoreWim()`은 설정과 상관없이 상대 값을 exe 폴더 기준으로 읽음. 켜고 끄면 새 형식으로 다시 씀). 백업 탭은 `image_folder`를 전체 경로로 둡니다. GUI 프로세스는 exe 폴더를 현재 폴더로 하여 실행되므로(main.cpp, 명령줄의 파일을 전체 경로로 바꾼 뒤) 탭에 입력한 상대 경로는 exe 폴더 기준입니다. `StartHelper`는 job.ini 전에 경로 키 `wim` / `source` / `folder` / `swm`을 전체 경로로 바꿉니다. `WimSmith.exe <파일>`은 `open_tab` 탭에, `WimSmith.exe --open <탭> <파일>`은 그 탭에 파일을 엽니다(`App::OpenFile`, 첫 창에서만). `open_all_tabs = 1`(기본, [설정])이면 다른 탭도 고른 탭보다 먼저 조용히 그 파일을 받습니다: 복구, 내보내기, 정보, 탐색은 항상, 백업 대상(+ 새 이미지 추가)과 나누기는 `.wim`만, 합치기는 `.swm`만.
- 한국어와 영어 UI, 다크나 라이트 테마([설정]에서 고르고 `WimSmith.ini`에 `theme = dark | light`로 저장). 백업 탭은 옵션 아래에 작업 미리 보기를 보여 줍니다. 예전 라이트 UI는 `backup\ui-classic\`에 있습니다(그 안의 `build_gui.bat`로 빌드).

## 필요한 것

- Windows 8.1 / 10 / 11 x64. `WimSmith.exe`는 네이티브 exe 하나(C++, 정적 C 런타임)이며 .NET Framework 같은 런타임이 필요 없습니다. x64 WinPE에서도 실행됩니다(탐색기 없이: 파일 대화상자를 직접 그림).
- `WimSmith.exe` 옆의 `lib\`에 제3자 DLL `libwim-15.dll`([wimlib](https://wimlib.net), x64)이 있습니다. 없으면 처음 필요할 때 **최신 버전을 자동으로 받습니다**(wimlib.net의 zip을 `src\unzip.cpp`로 풂: 7-Zip이나 tar.exe 필요 없음. 아무것도 설치하지 않음). `lib\versions.ini`에 버전을 기록합니다. 예전 `pe_tools\` 폴더의 `libwim-15.dll`은 시작할 때 `lib\`로 옮깁니다. 인터넷이 없으면 직접 넣으세요. 이 저장소에는 들어 있지 않으며, 라이선스 문서는 `lib\`에 함께 둡니다([제3자 프로그램](#제3자-프로그램) 참고).
- 백업, 복구, 디스크 탭에는 관리자 권한이 필요합니다(Windows가 매번 물음). 내보내기, 나누기 / 합치기, 정보, 검증, 탐색에는 필요 없습니다.

## 빌드

```
build.bat
```

`src\*.cpp`를 Visual Studio 2017 이상의 Visual C++(Community나 Build Tools, 워크로드 "C++를 사용한 데스크톱 개발". vswhere로 찾음)로 이 폴더의 `WimSmith.exe`(x64)로 컴파일합니다. 오브젝트 파일은 `obj\`에 생깁니다. 언어 파일 `lang\*.ini`는 exe에 들어가며, exe 옆의 고친 `lang\` 폴더를 먼저 씁니다.

wimlib은 실행 중에 불러오는 별도 DLL(`lib\libwim-15.dll`)로 둡니다.

예전 C# / .NET Framework 버전(C++ 이식 전까지)은 그것을 빌드하는 `build_gui.bat`와 함께 `backup\cs-winforms\`에 있습니다.

### 릴리스

GitHub 릴리스에는 zip이 두 번 들어갑니다: `WimSmith-<버전>-x64.zip`과 같은 파일인 **`WimSmith-x64.zip`**(`WimSmith.exe`, `lang\`, `LICENSE`, README, 사용 설명서. `lib\`은 없음). PhoenixPE 플러그인이 `releases/latest/download/WimSmith-x64.zip`을 받으므로 이 이름은 바꾸면 안 됩니다. 함께 올리는 파일: `phoenixpe\WimSmith.script`.

릴리스 노트는 한국어가 먼저이고 영어는 같은 본문 아래에 둡니다. 첫 줄은 `[English](#user-content-english)`, 영어 절 앞에는 `<a id="english"></a>`(릴리스 페이지는 제목에 앵커를 붙이지 않음), 그 아래 `## English`와 `###` 제목들입니다.

### PhoenixPE 플러그인

`phoenixpe\WimSmith.script`(PEBakery 스크립트, PhoenixPE의 스크립트처럼 LF, BOM 없는 UTF-8)는 PhoenixPE 빌드에 WimSmith를 넣습니다. `<PhoenixPE>\Projects\MyApps\Applications\`에 복사해 설치합니다(이 PC: `F:\PE\PhoenixPE-2026.07.25-x64`). 원본: 최신 GitHub 릴리스나 로컬 폴더(`WimSmith.exe`, `lang\`, 있으면 `lib\`). `lib\`이 없으면 빌드할 때 wimlib(버전은 인터페이스의 `txt_WimlibVersion`)을 wimlib.net에서 받아 라이선스 문서(`wimlib_COPYING*.txt`, `versions.ini`)와 함께 `lib\`에 넣습니다. PE에는 인터넷이 없는 경우가 많기 때문입니다. `WimSmith.ini`를 쓰고(`language`는 항상 설정: 고른 것, 또는 "PE 언어"면 `%SourceLang%`이 `ko-*`일 때 `ko`, 아니면 `en`. 테마, `open_tab`), `.wim` / `.esd` / `.swm`을 연결하며(PhoenixAPI `Associate`), HKLM 아래에 오른쪽 클릭 메뉴 "WimSmith >"를 넣습니다(`SystemFileAssociations`, `assoc::RegisterMenu`와 같은 항목). 메뉴 문구는 `lang\<코드>.ini`에서 `IniRead`로 읽으므로(섹션 `ui`, `RegisterMenus`와 같은 키) 스크립트는 ASCII로 유지됩니다. PhoenixAPI `Associate`는 exe를 `DefaultIcon`으로 씁니다. WimSmith 1.2 이상(그 `lang\en.ini`에 `rs.drivers`가 있음)이면 섹션 `SetFileIcon`이 파일 아이콘(`"<exe>",-2 / -3 / -4`)을 `<ext>file\DefaultIcon`에 씁니다.

## 파일

| 경로 | 내용 |
|---|---|
| `src\main.cpp` | 진입점: 창, 또는 `--wim-helper` |
| `src\app.*`, `src\mainwin.*` | 탭이 함께 쓰는 것(볼륨, 확인, 작업 실행), 메인 창, 바닥줄과 로그 창 |
| `src\tab_*.*` | 탭: 복구, 디스크, 백업, 내보내기, 나누기 / 합치기, 정보, 탐색, "?"(`tab_about`: 버전, 제작자, 이메일, 웹 페이지, 라이선스 전문, wimlib) |
| `src\helper.*` | 권한 상승된 helper(job.ini -> status.txt): 백업, 복구, 내보내기, 나누기 / 합치기, 검증, bcdboot |
| `src\disk.*`, `src\storage.*` | 디스크 탭: 디스크, 파티션, 볼륨(Windows의 IOCTL, PowerShell 없음), 디스크 지우기 / 초기화, 포맷(FormatEx), 드라이브 문자, diskpart 스크립트, 디스크 작업 |
| `src\wim.*`, `src\wimread.cpp` | wimlib(`lib\libwim-15.dll`, 실행 중에 불러옴), WIM 파일과 이미지 정보 |
| `src\libs.*`, `src\unzip.*` | `lib\` 폴더: wimlib 옮기기와 받기(WinHTTP), 라이선스 문서. zip 읽기(stored / Deflate, CRC-32) |
| `src\ui.*` | Win32 위의 창 툴킷: 다크 컨트롤, 탭, 목록, 대화상자, 행 배치 |
| `src\lang.*`, `src\settings.*`, `src\util.cpp`, `src\common.h` | 언어 파일, `WimSmith.ini`, 텍스트 / 파일 / 프로세스 도우미 |
| `src\assoc.*` | 현재 사용자의 `.wim` / `.esd` / `.swm` WimSmith 연결(HKCU 레지스트리, 권한 상승 없음) |
| `src\makewim.*` | 폴더 메뉴의 "WIM 파일로 만들기": 고른 모든 폴더를 위한 창 하나, 메인 창 없이 |
| `src\extractwim.*` | WIM 파일 메뉴의 압축 풀기 / 각각 압축 풀기: 파일 옆의 새 폴더, 권한 상승 없이 |
| `src\gather.*` | 탐색기 메뉴 한 번의 클릭으로 들어온 파일 / 폴더를 첫 프로세스로 모음(뮤텍스 + WM_COPYDATA) |
| `src\net.*` | 네트워크의 파일을 읽는 링크(권한 상승 없음): UNC나 네트워크 드라이브(`WNetGetConnection`)의 서버 → 주소(`GetAddrInfoW`) → Windows가 고르는 인터페이스(`GetBestInterfaceEx`) → 받기 링크 속도(`GetIfEntry2`). `iphlpapi.dll`은 실행 중에 불러옴, 루프백은 제외 |
| `src\usb.*` | 드라이브가 USB로 어떻게 연결되어 있는지(권한 상승 없음): 디스크 인터페이스 → 장치 트리의 USB 장치 → 허브 포트에 `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX(_V2)`. 속도, 포트의 USB 3.0 지원, PC에 USB 3.0 포트가 있는지 |
| `src\version.h` | 버전(1.3.1), 제작자, 이메일, 웹 페이지, 저작권 / 라이선스 줄: exe의 버전 리소스(파일 속성 -> 자세히), 창 제목, 로그 첫 줄, "?" 탭 |
| `app.rc`, `app.manifest` | 아이콘(1 = 프로그램, 2 / 3 / 4 = .wim / .esd / .swm 파일, `icons\`), 버전 리소스, `LICENSE`(리소스 형식 TEXT, "?" 탭에 표시), 내장 언어 파일. 매니페스트(DPI, Common Controls 6, Windows 버전) |
| `lang\en.ini`, `lang\ko.ini` | 문구(en.ini가 기준) |
| `phoenixpe\WimSmith.script` | PhoenixPE 플러그인([PhoenixPE 플러그인](#phoenixpe-플러그인) 참고) |

권한 상승된 helper는 `%TEMP%\WimSmith_job\<시각>`(job.ini, status.txt, wimlib_errors.txt)에서 일합니다. 작업이 성공하면 폴더를 지우고, 실패하면 남겨 둡니다.

## 작업할 때

- 소스 파일은 CRLF 줄 끝을 씁니다. `sed -i`는 LF로 바꾸므로 `unix2dos`로 고치거나 CRLF를 유지하는 스크립트로 편집하세요. 언어 파일은 BOM 있는 UTF-8입니다.
- `lang\en.ini`가 기준입니다. 새 문구는 모두 여기와 `lang\ko.ini`에 넣으세요. 시작 버튼 아래의 안내 문구는 일부러 없앴습니다. 설명은 사용 설명서에 씁니다.
- 문서는 한국어판(`<이름>.md`)과 영어판(`<이름>.en.md`)을 함께 고칩니다.
- 화면 낭독기(MSAA): 입력 칸의 이름은 z-순서에서 바로 앞의 `Static`에서 옵니다. 그래서 `Grid::Add`는 행 라벨을 그 행의 첫 컨트롤 앞으로 옮기고, 행의 뒤쪽 칸(체크박스 뒤)에는 `AccName`으로 이름을 붙입니다. 앞에 라벨이 없는 컨트롤도 `AccName`을 받습니다. 체크박스(`WimSmithCheck`)와 진행 막대는 Dynamic Annotation(`AccPropServices`, COM으로 만듦: exe는 `oleacc.dll`을 가져오지 않음)으로 역할 / 상태 / 값을 알립니다. 탭 줄은 자체 `IAccessible`(`TabsAccessible`, `WM_GETOBJECT`, `oleacc.dll`은 실행 중에 불러옴)을 가집니다. 링크(`ui::Link`)는 직접 그리는 버튼(kind 3)이라 탭 순서에 들어갑니다. 지우는 작업의 확인은 `ui::Confirm`을 씁니다(task dialog, 할 일을 이름으로 단 버튼, 기본은 *취소*. `TaskDialogIndirect`는 실행 중에 찾고, 없으면 예 / 아니요 상자). Ctrl+Tab / Ctrl+PgDn이 탭을 바꿉니다(메시지 루프의 `TabKeys`).
- 팝업 메뉴는 `ui::PopupMenu`를 거칩니다(직접 그리는 항목, 메뉴 배경, #32768 창의 테두리): Windows의 메뉴는 다크 테마에서도 밝게 남기 때문입니다. `TrackPopupMenu`를 직접 부르지 마세요.
- 실행 중에는 PowerShell을 쓰지 않습니다(많은 WinPE 이미지에 없음): 디스크, 볼륨, 포맷은 `src\storage.*`를 거칩니다.
- 권한 상승된 작업은 세션의 네트워크 드라이브나 SUBST 드라이브를 볼 수 없습니다: helper에 넘기는 경로는 `SessionFreePath`를 거칩니다.
- 작은 화면(비디오 드라이버 없는 WinPE, 640 x 480): 모든 탭 페이지는 스크롤 호스트 안에 있습니다(`ui::ScrollHost`, 최소 828 x 490, 탐색 / 정보는 `SetScrollMin`으로 더 작게). 900 x 600이 들어가지 않으면 메인 창은 최대화로 시작하고, 창과 대화상자는 화면 안에 둡니다(`FitToWorkArea`). 탭 줄과 바닥줄은 좁아집니다. 새 탭이나 대화상자도 그 크기에서 동작해야 합니다: 대화상자는 고정 좌표가 아니라 클라이언트 크기에서 배치하세요.
- `WimSmith.exe`가 실행 중이면 `build.bat`가 그것을 바꿀 수 없습니다: 먼저 닫거나 다른 폴더로 빌드하세요.
- 시험: exe 사본을 별도의 `WimSmith.ini`와 함께 임시 폴더에서 쓰세요. 사용자의 ini를 고치지 말고, 사용자가 실행 중인 프로그램을 닫지 말고, 시험에서 권한 상승 작업을 시작하지 마세요(UAC 창이 사용자의 화면에 뜸). 권한 상승이 필요 없는 작업(폴더 백업, 검증 등)은 helper를 직접 실행할 수 있습니다(`WimSmith.exe --wim-helper <job.ini가 있는 폴더>`).
- 지난 세션에서 배운 것은 `.trail/learnings/`에 있습니다.

## 라이선스

MIT, [LICENSE](LICENSE) 참고. 이 저장소의 코드에만 적용됩니다.

### 제3자 프로그램

WimSmith는 별도 프로그램 하나를 씁니다. 그 코드는 이 저장소에 없으며, 자체 라이선스를 따릅니다.

| 프로그램 | `lib\`의 파일 | 라이선스 | `lib\`의 라이선스 문서 | 원본 |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | Windows용 라이브러리는 LGPL v2.1 이상(또는 GPL v3 이상). libdivsufsort-lite(MIT) 포함 | `wimlib_COPYING.txt`, `wimlib_COPYING.LGPL.txt`, `wimlib_COPYING.GPLv3.txt`, `wimlib_COPYING.libdivsufsort-lite.txt` | https://wimlib.net/ |

WimSmith가 쓰는 방식:

- wimlib은 별도 DLL(`libwim-15.dll`)로 불러와 공개 API로 호출합니다. 고치지 않았으며, `lib\`의 DLL을 바꾸면 다른 버전으로 바꿀 수 있습니다.
- 저장소에는 들어 있지 않습니다. WimSmith가 사용자의 PC에서 wimlib.net으로부터 라이선스 문서와 함께 받습니다. DLL만 있고 라이선스 문서가 없으면(예: `pe_tools\`에서 옮긴 `libwim-15.dll`) 시작할 때 백그라운드에서 문서만 받습니다.

WimSmith를 `lib\`과 **함께** 전달할 때(예: zip 파일로):

- 위의 라이선스 문서를 `lib\`에 두세요.
- 이 파일이 자체 라이선스를 따르는 wimlib이라는 것을 위의 원본 링크와 함께 밝히세요(WimSmith가 받았다면 wimlib의 버전은 `lib\versions.ini`에 있음).
- DLL을 고치지 마세요. 고치면 고친 소스를 같은 라이선스로 공개해야 합니다.
