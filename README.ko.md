<p align="left">
    <img src="src/Resources/icons/pinpointstudio_1024.png" width="150" />
  </p>

[English](README.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [日本語](README.ja.md) · **한국어** · [简体中文](README.zh-CN.md)

> 참고: 이 파일은 README.md의 번역본이며, 링크된 문서는 영어로만 제공됩니다.

# PinPoint Studio

PinPoint Studio는 골프 스윙 분석을 위한 무료 오픈소스 크로스 플랫폼 데스크톱 애플리케이션입니다.
고속 카메라, 스마트폰, 웨어러블 센서, 론치 모니터를 하나로 연결하고,
기기에서 직접 실행되는 컴퓨터 비전으로 스윙을 측정한 뒤 그 결과를 설명해 줍니다.
사용자가 직접 선택하지 않는 한 데이터는 어디로도 전송되지 않습니다.

골퍼, 코치, 연구자를 위해 만들어졌으며 Windows, macOS, Linux에서 실행됩니다.

> **상태:** 베타(v0.1). 캡처, 분석, 리뷰, 진단은 처음부터 끝까지 동작하며,
> 지면 반력 세션과 AI 코치 세션은 아직 개발 중입니다.

<img width="2560" height="1440" alt="beta3-screenshot" src="https://github.com/user-attachments/assets/c060ba3b-2927-4380-8c58-a34cc4c424a2" />

## 주요 기능

- **멀티 카메라 캡처** — USB 웹캠, GenICam 산업용 카메라(Aravis), Teledyne FLIR 고속 카메라(Spinnaker)를 지원하며,
  임팩트를 근접 촬영하는 전용 카메라도 함께 사용할 수 있습니다.
- **스마트폰을 카메라로** — PinPoint Capture를 실행한 iPhone이 WiFi 또는 USB 케이블을 통해
  암호화된 연결로 세션에 참여합니다.
- **마커리스 스윙 추적** — 전신 포즈 추정(ViTPose), 테이프나 마커가 필요 없는 클럽 샤프트 및 클럽 헤드 추적,
  볼 검출을 수행합니다.
- **자동 샷 감지** — IMU로 감지한 임팩트, 임팩트 소리 또는 론치 모니터를 통해 샷을 감지하고,
  샷마다 그 자리에서 분석해 저장합니다.
- **스윙 지표** — 어드레스부터 피니시까지의 열 가지 P-positions, 템포, 클럽 딜리버리(club delivery), 몸통 회전, 자세,
  손목 각도, 키네마틱 시퀀스(kinematic sequence)를 측정하며,
  각 지표에는 얼마나 신뢰할 수 있는지가 함께 표시됩니다.
- **진단** — 기준 범위에 비추어 스윙 오류와 강점을 짚어 내고,
  샷마다, 그리고 세션 전체에 걸쳐 예상 원인과 교정 드릴을 함께 제시합니다.
- **손목 분석** — Witmotion IMU 또는 HackMotion wG3 센서로
  스윙 전 구간에 걸친 리드 손목의 굴곡(flexion), 신전(extension), 코킹을 측정합니다.
- **론치 모니터** — Foresight GCQuad(FSX2020 경유)와
  GSPro Open Connect 브리지가 있는 모든 기기(Garmin R10, Rapsodo MLM2PRO, SkyTrak+ 등)를 지원합니다.
  측정값은 그 값을 만들어 낸 스윙과 짝지어집니다.
- **리뷰** — 오버레이, 차트, 스윙 단계 타임라인과 함께 모든 카메라 영상을 동기화해 재생하며,
  모든 세션을 디스크에 보관하는 스윙 라이브러리를 제공합니다.
- **정답 데이터(ground truth) 마크업** — 분석을 검증하고 튜닝할 수 있도록,
  앱 안에서 P-positions를 프레임 단위로 정확하게 라벨링합니다.
- **음성** — 로컬 음성 인식(whisper.cpp)과 음성 합성(Kokoro)을 제공하며,
  필요하면 클라우드 서비스를 대체 수단으로 쓸 수 있습니다.
- **로컬 우선, 프라이버시 보호** — 분석은 사용자의 컴퓨터에서 실행되며,
  가능한 경우 GPU 가속(CUDA, Vulkan, CoreML, Metal)을 사용합니다.
  텔레메트리는 없으며, 클라우드 서비스는 직접 설정하지 않는 한 꺼져 있습니다.
- **앱 내 업데이트** — Windows와 macOS용 서명된 설치 프로그램을 제공하며, 설치한 앱은 재설치 없이 스스로 업데이트됩니다.
  자동 업데이트되는 Linux AppImage는 준비 중이며, 그때까지 Linux에서는 소스에서 직접 빌드해야 합니다.

## 설치하기

설치 프로그램은
[릴리스 페이지](https://github.com/PinPoint-Golf/PinPointStudio/releases)에 게시됩니다.
소스에서 빌드하려면 **[BUILDING.md](BUILDING.md)** 문서를 참고하세요.

## 저장소 구조

| 경로 | 내용 |
|---|---|
| `src/` | 애플리케이션 본체. 서브시스템마다 폴더 하나(아래 참고) |
| `tests/` | 전체 테스트를 묶는 상위 테스트 빌드. 각 테스트 스위트는 해당 코드 옆의 `src/<area>/tests/`에 위치 |
| `tools/` | 오프라인 및 실험용 도구: SwingLab(녹화된 스윙에 분석 파이프라인을 다시 실행), 샤프트/볼/임팩트 랩, 론치 모니터 모의 장치, 패키징 스크립트 |
| `packaging/` | Windows 설치 프로그램 및 appcast 스크립트, macOS 패키징 자산 |
| `cmake/` | 빌드 모듈: Qt 탐색, QML 모듈, 글꼴, OpenSSL, 코드 서명 |
| `third_party/` | 저장소에 직접 포함한 소규모 외부 소스(ESKF 자세 추정 필터) |
| `docs/` | 문서. 대상 독자별로 정리(아래 참고) |

`src/` 내부:

| 폴더 | 서브시스템 |
|---|---|
| `Gui/` | Qt Quick / QML 인터페이스와 그 C++ 모델 |
| `Video/`, `Pose/` | 카메라 백엔드, 포즈·볼 모델 및 인물 세그멘테이션 모델 |
| `Analysis/`, `Metrics/`, `Diagnostics/` | 스윙 분석 파이프라인, 지표 카탈로그, 스윙 오류 진단 |
| `Buffer/` | 모든 센서를 시간축에 맞춰 정렬하는 락프리(lock-free) 이벤트 버퍼 |
| `IMU/`, `LaunchMonitor/`, `Ppcp/`, `Audio/` | 센서, 론치 모니터, 스마트폰 캡처, 마이크와 샷 소리 |
| `Export/` | 스윙 영상과 `swing.json` 문서 |
| `STT/`, `TTS/`, `LLM/`, `Secrets/` | 음성 처리, AI 코치, API 키 저장 |
| `Update/`, `Core/`, `Resources/`, `Shaders/` | 앱 내 업데이트, 공용 인프라, 번들 자산, GPU 셰이더 |

## 문서

| 위치 | 용도 |
|---|---|
| [BUILDING.md](BUILDING.md) | 의존성, 플랫폼별 빌드, 패키징, 테스트 실행 |
| [`docs/user/`](docs/user) | 앱 사용법: [진단 가이드](docs/user/pinpoint-diagnostics-guide.md), [손목 캘리브레이션](docs/user/wristcalibration.md), [스마트폰 캡처](docs/user/phone_capture.md), UX 설계 |
| [`docs/developer/`](docs/developer) | 개발자 가이드: [분석 파이프라인](docs/developer/analysis_pipeline_developer_guide.md), [테스트](docs/developer/testing_developer_guide.md), [SwingLab](docs/developer/swinglab_developer_guide.md), [기능 스위치](docs/developer/feature_switches_developer_guide.md), [번역](docs/developer/translations_developer_guide.md), 이벤트 버퍼, 진단, 지표 |
| [`docs/design/`](docs/design) | 서브시스템별 설계 기록과 [QML 디자인 시스템](docs/design/pinpoint_qml_design_system.md) |
| [`docs/reference/`](docs/reference) | 파일 형식([`swing.json`](docs/reference/swing_json_schema.md), [스윙 폴더](docs/reference/swing_folder_layout.md)), 부호 규약, 기기 프로토콜, 기준 데이터 |
| [`docs/validation/`](docs/validation), [`docs/research/`](docs/research) | 검증 프로토콜, 코퍼스 결과, 연구 노트 |
| `docs/implementation/` | 작업 계획과 릴리스 런북 |

## 관련 프로젝트

다음 자매 저장소들이 의존성으로 사용됩니다:

- [libppcp](https://github.com/PinPoint-Golf/libppcp) — 스마트폰 앱과 공유하는 PinPoint Capture Protocol
- [PinPoint Capture](https://github.com/PinPoint-Golf/PinPointCapture) — iPhone용 캡처 앱
- [libwrist](https://github.com/PinPoint-Golf/libwrist) — HackMotion wG3 센서 드라이버
- [libgspro](https://github.com/PinPoint-Golf/libgspro) — GSPro Open Connect 서버

## 라이선스

PinPoint Studio는 GNU General Public License 버전 2 또는 그 이후 버전을 따릅니다.
배포되는 바이너리는 링크하는 구성 요소 때문에 GPL-3.0에 따라 제공됩니다.
자세한 내용은 [LICENSE](LICENSE)를, 모든 서드파티 구성 요소와 각각의 조건은
[LICENSEDEPS.md](LICENSEDEPS.md)를 참고하세요.
