# WimSmith

볼륨이나 폴더를 WIM 이미지로 백업하고, WIM 이미지를 볼륨이나 폴더에 복구하는 Windows 도구입니다. 설치 없이 exe 하나로 실행되며, Windows PE에서도 쓸 수 있습니다.

[English](README.md) · 사용 설명서: [MANUAL.ko.md](MANUAL.ko.md)

## 할 수 있는 일

- **백업**: 볼륨이나 폴더를 WIM 파일로 저장합니다. VSS 스냅샷으로 실행 중인 Windows도 백업할 수 있습니다. 기존 파일에 추가할 때는 바뀌지 않은 파일을 마지막 이미지에서 가져와 빠르고, 제외 목록으로 임시 파일과 캐시를 뺄 수 있습니다. 백업 뒤 검증과 무결성 테이블(`--check`)을 고를 수 있습니다.
- **복구**: WIM / ESD / SWM 이미지를 볼륨(포맷 후)이나 빈 폴더에 풀고, DISM으로 드라이버를 추가하며(Windows의 DISM 또는 ADK 등 폴더의 DISM), UEFI / BIOS 부트 파일을 씁니다. Compact OS를 고르면 Windows 10 압축으로 풀어 수 GB를 아낍니다.
- **디스크**: 디스크를 GPT 또는 MBR로 초기화하고 Windows 설치용 파티션을 만듭니다.
- **내보내기**: 압축 방식 바꾸기, ESD → WIM 변환, 여러 WIM을 하나로 모으기.
- **나누기 / 합치기**: FAT32 USB에 담을 수 있게 `.swm` 조각으로 나누고 다시 합칩니다.
- **정보 / 검증**: WIM 파일과 이미지 정보를 보고, 파일이 손상되지 않았는지 확인합니다. 이미지 이름·설명·에디션과 부팅 이미지를 바꾸고, 이미지를 지우고, 파일을 최적화(크기 줄이기)합니다.
- **탐색**: 이미지 안의 파일을 보고 필요한 파일만 꺼냅니다. 이미지 안에 파일을 추가하고, 이름을 바꾸고, 지울 수 있습니다.

한국어 / 영어 화면, 다크 / 라이트 테마를 지원합니다. [설정]에서 `.wim`, `.esd`, `.swm` 파일을 WimSmith로 열도록 연결할 수 있고, 열 때 보여 줄 탭(기본: 탐색)도 고를 수 있습니다. 이 파일들의 오른쪽 클릭 메뉴에 **WimSmith ▸** 하위 메뉴(복구, 이미지 추가, 내보내기, 나누기 / 합치기, 정보, 탐색, Windows PE `.wim`은 RAM 디스크로 한 번 부팅)를 넣을 수도 있습니다.

## 필요한 것

- Windows 8.1 / 10 / 11 x64 또는 x64 Windows PE
- 백업, 복구, 디스크 작업에는 관리자 권한 (실행할 때마다 Windows가 묻습니다)

.NET 같은 런타임은 필요 없습니다.

## 배포 파일

| 파일 | 설명 |
|---|---|
| `WimSmith.exe` | 프로그램 |
| `lang\` | 언어 파일 (exe에도 들어 있으며, 이 폴더의 파일을 먼저 씁니다. 화면 문구를 고칠 수 있습니다) |
| `MANUAL.ko.md`, `MANUAL.md` | 사용 설명서 (한국어, 영어) |
| `README.ko.md`, `README.md` | 이 문서 |
| `LICENSE` | WimSmith의 라이선스 |

배포 파일에는 wimlib이 없습니다. wimlib이 처음 필요할 때 `WimSmith.exe` 옆에 `lib\` 폴더(`libwim-15.dll`과 그 라이선스 문서)가 만들어집니다.

## 시작하기

1. 압축을 원하는 폴더(USB 가능)에 풉니다.
2. `WimSmith.exe`를 실행합니다.
3. 설정은 같은 폴더의 `WimSmith.ini`에 저장됩니다.

`lib\` 폴더의 파일이 없으면 처음 필요할 때 인터넷에서 최신 버전을 자동으로 받습니다. `WimSmith.exe`와 `lib\` 폴더는 같은 폴더에 있어야 합니다. 인터넷이 없는 곳에서 쓰려면 `lib\` 폴더를 지우지 말고 함께 복사하세요.

자세한 사용 방법은 [사용 설명서](MANUAL.ko.md)를 보세요.

### PhoenixPE

[`phoenixpe/WimSmith.script`](phoenixpe/WimSmith.script)는 [PhoenixPE](https://github.com/PhoenixPE/PhoenixPE) 빌드에 WimSmith를 넣는 플러그인입니다. PhoenixPE의 `Projects\MyApps\Applications\`에 복사하세요. 이 저장소의 최신 릴리스나 로컬 폴더에서 WimSmith를 가져오고, 빌드할 때 wimlib을 `lib\`에 넣으며(PE에는 인터넷이 없는 경우가 많음), 바로 가기, 파일 연결, 오른쪽 클릭 메뉴를 추가할 수 있습니다.

## 주의

- **복구**는 대상 볼륨을 포맷하고, **디스크** 작업은 디스크 전체를 지웁니다. 실행 전에 대상을 꼭 확인하세요. 두 작업 모두 두 번 확인하며, 마지막에는 드라이브 문자나 디스크 번호를 직접 입력해야 합니다.
- 실행 중인 Windows의 드라이브와 디스크, WimSmith가 있는 드라이브는 복구나 초기화 대상으로 고를 수 없습니다.

## 라이선스

WimSmith의 코드는 MIT 라이선스입니다([LICENSE](LICENSE)).

`lib\` 폴더의 wimlib은 WimSmith의 일부가 아니며 자체 라이선스를 따릅니다. WimSmith는 wimlib을 고치지 않고 별도 DLL로 불러 씁니다. DLL을 바꾸면 다른 버전으로 교체할 수 있습니다.

| 프로그램 | 파일 | 라이선스 | 라이선스 문서 | 소스 |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | LGPL v2.1 이상(또는 GPL v3 이상), libdivsufsort-lite(MIT) 포함 | `wimlib_COPYING*.txt` | https://wimlib.net/ |

다시 배포할 때는 `lib\`의 라이선스 문서를 그대로 두세요. wimlib 버전은 `lib\versions.ini`에 적혀 있습니다.

이제 7-Zip은 쓰지 않습니다. 이전 버전의 `lib\`에 남아 있는 `7z.exe`, `7z.dll`, `7zFM.exe`, `7-Zip_License.txt`는 지워도 됩니다.
