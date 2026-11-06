# 7z_wimlib

[wimlib](https://wimlib.net)으로 볼륨이나 폴더를 WIM 이미지로 백업하고 복구하는 작은 Windows 도구입니다. DVT GUI의 "백업 / 복구" 페이지를 떼어 내 만들었습니다.

[English](README.md)

## 기능

- **백업**: 볼륨이나 폴더를 새 WIM 파일로, 또는 기존 WIM 파일의 새 이미지로 저장합니다(LZX / XPRESS / LZMS, 스레드 수 지정). **VSS 스냅샷**을 켜면 볼륨 섀도 복사본(wimlib의 snapshot 옵션)에서 읽어, 사용 중인 파일도 그 시점 그대로 저장하고 실행 중인 Windows 드라이브도 저장할 수 있습니다(이때 WIM 파일은 다른 드라이브에 두어야 합니다). 스냅샷 없이는 실행 중인 Windows 드라이브를 저장할 수 없습니다.
- **볼륨에 복구**: 볼륨을 포맷(NTFS)한 뒤 이미지 하나를 볼륨 루트에 적용합니다. 두 번 확인합니다. Windows 드라이브, 7z_wimlib이 있는 드라이브, WIM 파일이 있는 드라이브는 목록에 없습니다.
- **폴더에 복구**: 포맷하지 않고 비어 있는 폴더 안에 이미지를 풉니다.
- **디스크 (파티션)**: 디스크 전체를 지우고 GPT 또는 MBR로 초기화한 뒤 파티션을 만듭니다. 두 번 확인하며, 두 번째에는 디스크 번호를 직접 입력해야 합니다. *Windows 설치용*: GPT는 EFI 시스템(FAT32, 260 MB) + MSR(16 MB, 빼도 됨) + Windows(NTFS), MBR은 시스템 예약(NTFS, 500 MB, 활성) + Windows이고, Windows 뒤에 복구 파티션을 더할 수 있습니다. *Windows + 데이터*: 같은 구성 맨 끝에 데이터 파티션(NTFS / exFAT / FAT32)을 더합니다. 남는 공간 중 Windows가 정한 % 비율만큼, 데이터 파티션이 나머지를 가집니다. *데이터 파티션 1개*: 디스크 전체를 NTFS / exFAT / FAT32 파티션 하나로 만듭니다. 실행 중인 Windows의 디스크와 7z_wimlib가 있는 디스크는 고를 수 없습니다. Windows가 들어가는 구성으로 만들면 복구 탭이 새 파티션을 대상으로, 새 시스템 파티션에 UEFI(GPT) 또는 BIOS(MBR) 부트 파일을 쓰도록 설정됩니다.
- 복구와 내보내기에는 나눈 WIM(`.swm`)도 쓸 수 있습니다. 아무 조각이나 고르면 같은 폴더에서 나머지 조각을 찾습니다(이름을 바꾸지 않은 모든 조각이 있어야 합니다).
- **내보내기**: WIM / ESD 파일의 이미지 하나 또는 전체를 새 WIM 파일이나 기존 WIM 파일로 복사합니다(압축 방식 바꾸기, ESD -> WIM, 여러 파일을 하나로 모으기, 이름·설명 바꾸기). 부팅 이미지 표시는 유지합니다. 관리자 권한이 필요 없습니다.
- **나누기 / 합치기**: WIM 파일을 정한 크기의 `.swm` 조각으로 나누고(예: FAT32 USB용 4000 MB), 조각들을 다시 하나의 WIM 파일로 합칩니다(아무 조각이나 고르면 나머지를 찾습니다). solid 리소스가 있는 WIM(ESD / LZMS solid)은 나눌 수 없으니 먼저 LZX로 내보내세요. 관리자 권한이 필요 없습니다.
- **정보**: WIM 파일 정보(이미지 수, 압축, 크기, 부팅 이미지, GUID, 플래그)와 이미지별 정보(에디션, 아키텍처, 버전, 언어, 크기, 파일 수, 날짜)를 보여 줍니다. 클립보드로 복사하거나 고른 이미지를 복구 / 내보내기 탭으로 넘길 수 있습니다.
- **검증**: 정보 탭의 [검증]은 WIM / ESD / SWM 파일의 모든 데이터를 읽어 SHA-1 해시와 이미지 메타데이터를 확인하고, 무결성 테이블이 있으면 그것도 확인합니다(`wimlib_verify_wim`). 백업 탭에서 "백업이 끝나면 WIM 파일을 검증"을 켜면 백업 직후 같은 검사를 합니다. 파일은 바꾸지 않습니다.
- **탐색**: 7-Zip으로 이미지 안의 파일을 봅니다. 폴더 트리와 파일 목록, 선택한 파일 / 폴더를 폴더에 풀기, 파일 열기, 7-Zip 파일 관리자에서 열기. WIM 파일은 바꾸지 않습니다.
- **부트 파일**: 볼륨에 복구한 뒤 `bcdboot`로 고른 시스템 파티션에 UEFI / BIOS 부트 파일을 씁니다(부트 관리자 언어 지정, 기본 `ko-kr`). 문자가 없는 파티션은 잠시 문자를 할당합니다.
- **완료 후**: 아무것도 안 함 / 시스템 종료 / 다시 시작. 60초 카운트다운 중에 취소할 수 있습니다.
- 한국어 / 영어 화면. 어두운 테마이며, 백업 탭 오른쪽에 작업 미리보기가 있습니다. 예전 밝은 화면은 `backup\ui-classic\`에 보관되어 있습니다(그 폴더의 `build_gui.bat`으로 빌드).

## 필요한 것

- Windows 10/11 x64, .NET Framework 4.x(기본 포함).
- `7z_wimlib.exe` 옆의 `lib\` 폴더에 외부 프로그램을 둡니다: `libwim-15.dll`([wimlib](https://wimlib.net), x64)과 `7z.exe`, `7z.dll`, `7zFM.exe`([7-Zip](https://www.7-zip.org), x64). 없는 파일은 처음 필요할 때 **최신 버전을 자동으로 받습니다**(wimlib은 wimlib.net, 7-Zip은 7-zip.org / GitHub 릴리스에서 받아 `7zr.exe`로 풀며, 설치하지 않습니다). 버전은 `lib\versions.ini`에 기록하고, 예전 `pe_tools\` 폴더의 `libwim-15.dll`은 시작할 때 `lib\`로 옮깁니다. 인터넷에 연결되어 있지 않으면 이 파일들을 직접 넣으세요. 이 저장소에는 들어 있지 않으며, 각 프로그램의 라이선스 문서를 `lib\`에 함께 둡니다([외부 프로그램](#외부-프로그램) 참고).
- 백업, 복구, 디스크 탭에는 관리자 권한이 필요합니다(매번 Windows가 묻습니다). 내보내기, 나누기 / 합치기, 정보, 검증, 탐색은 필요 없습니다.

## 빌드

```
build_gui.bat
```

.NET Framework의 C# 컴파일러로 `src\*.cs`를 이 폴더의 `7z_wimlib.exe`로 빌드합니다(Visual Studio 필요 없음). 언어 파일 `lang\*.ini`는 exe에 들어가며, exe 옆에 `lang\` 폴더가 있으면 그쪽을 먼저 읽습니다.

### C++ 전환 (진행 중)

.NET Framework 없이 exe 하나로 실행되도록(WinPE 포함) 7z_wimlib를 C++(Win32 API, 정적 C 런타임)로 옮기고 있습니다. C# 버전은 `backup\cs-winforms`에 보관합니다.

```
cpp\build.bat
```

Visual Studio 2017 이상의 Visual C++(Community 또는 Build Tools, "C++를 사용한 데스크톱 개발", vswhere로 찾음)로 `cpp\src\*.cpp`를 `7z_wimlib_native.exe`(x64)로 빌드합니다.

- 1단계(완료): 도우미(백업, 복구, 내보내기, 나누기 / 합치기, 검증, 디스크). `7z_wimlib.exe` 옆에 `7z_wimlib_native.exe`가 있으면 창은 작업마다 자기 대신 이것을 실행합니다(로그에 "도우미: 7z_wimlib_native.exe (C++)"). 지우면 C# 도우미로 돌아갑니다.
- 2단계: 창(탭)을 옮기고, 그 뒤 C++ exe가 `7z_wimlib.exe`가 됩니다.

wimlib은 이전처럼 실행할 때 불러오는 별도 DLL(`lib\libwim-15.dll`)로 둡니다.

## 파일

| 경로 | 내용 |
|---|---|
| `src\Program.cs` | 시작점, 설정(`7z_wimlib.ini`), `--wim-helper` |
| `src\MainForm.cs` | 창, 상태 표시줄, 언어 선택 |
| `src\BackupPanel.cs` | 백업 / 복구 / 내보내기 / 정보 화면 |
| `src\WimBrowser.cs` | 탐색 탭 (7-Zip) |
| `src\Libs.cs` | `lib\` 폴더: wimlib, 7-Zip 옮기기와 내려받기 |
| `src\Theme.cs` | 어두운 화면(색, 탭, 진행 막대, 어두운 제목 표시줄 / 스크롤 막대) |
| `src\LogWindow.cs` | 로그 창 ([로그 보기]를 누를 때까지 숨김) |
| `src\BackupPanel.Disk.cs`, `src\DiskJob.cs` | 디스크 탭, 디스크 목록(Get-Disk), 초기화(Clear-Disk / Initialize-Disk), 파티션 생성(diskpart) |
| `src\WimLib.cs` | wimlib 호출, 관리자 권한 도우미, 파티션, bcdboot |
| `src\Lang.cs`, `src\Ui.cs`, `src\Controls.cs` | 언어 파일, UI 도우미 |
| `lang\en.ini`, `lang\ko.ini` | 문구(en.ini가 기준) |

관리자 권한 도우미는 `%TEMP%\DVT_wim_job\<시각>`(job.ini, status.txt, wimlib_errors.txt)에서 작업합니다. 성공하면 이 폴더를 지우고, 실패하면 남깁니다.

## 라이선스

BSD 3-Clause, [LICENSE](LICENSE) 참고. 이 저장소의 코드에만 적용됩니다.

### 외부 프로그램

7z_wimlib는 별도의 프로그램 두 개를 사용합니다. 그 코드는 이 저장소에 들어 있지 않으며, 각자의 라이선스를 따릅니다.

| 프로그램 | `lib\`의 파일 | 라이선스 | `lib\`의 라이선스 문서 | 소스 |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | Windows용 라이브러리는 LGPL v2.1 이상(또는 GPL v3 이상), libdivsufsort-lite(MIT) 포함 | `wimlib_COPYING.txt`, `wimlib_COPYING.LGPL.txt`, `wimlib_COPYING.GPLv3.txt`, `wimlib_COPYING.libdivsufsort-lite.txt` | https://wimlib.net/ |
| [7-Zip](https://www.7-zip.org) | `7z.exe`, `7z.dll`, `7zFM.exe` | GNU LGPL, 일부 BSD 3-Clause, unRAR 제한 | `7-Zip_License.txt` | https://www.7-zip.org/ |

7z_wimlib가 이 프로그램들을 쓰는 방식:

- wimlib은 별도 DLL(`libwim-15.dll`)로 불러 공개 API를 호출하고, 7-Zip은 별도 프로그램(`7z.exe`)으로 실행합니다. 어느 쪽도 수정하지 않았고, `lib\`의 파일을 바꾸면 다른 버전으로 교체할 수 있습니다.
- 이 저장소에는 들어 있지 않습니다. 7z_wimlib가 사용자의 PC에서 공식 사이트로부터 라이선스 문서와 함께 받습니다. 프로그램은 있는데 라이선스 문서가 없으면(예: `pe_tools\`에서 옮겨 온 `libwim-15.dll`) 시작할 때 문서만 백그라운드로 받습니다.

7z_wimlib를 `lib\`와 **함께** 전달할 때(예: zip 파일):

- 위 표의 라이선스 문서를 `lib\`에 그대로 두세요.
- 이 파일들이 각자의 라이선스를 따르는 wimlib / 7-Zip이라는 점과 위의 소스 링크를 밝히세요(7z_wimlib가 받은 경우 wimlib 버전은 `lib\versions.ini`에 있습니다).
- 프로그램을 수정하지 마세요. 수정했다면 수정한 소스를 같은 라이선스로 공개해야 합니다.
