<p align="left">
    <img src="src/Resources/icons/pinpointstudio_1024.png" width="150" />
  </p>

[English](README.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · **日本語** · [한국어](README.ko.md) · [简体中文](README.zh-CN.md)

> 注: このファイルは README.md の翻訳です。リンク先のドキュメントは英語版のみです。

# PinPoint Studio

PinPoint Studio は、ゴルフスイング解析のための無料かつオープンソースの、クロスプラットフォーム対応デスクトップアプリケーションです。ハイスピードカメラ、スマートフォン、ウェアラブルセンサー、弾道測定器（ローンチモニター）をひとつにまとめ、デバイス上で動作するコンピュータービジョンでスイングを計測し、わかったことを解説します。自分で選ばないかぎり、データが外部へ送信されることはありません。

ゴルファー、コーチ、研究者に向けて作られており、Windows、macOS、Linux で動作します。

> **ステータス:** ベータ版（v0.1）。キャプチャ、解析、レビュー、診断は最初から最後まで一貫して動作します。地面反力と AI コーチのセッションは現在開発中です。

<img width="2560" height="1440" alt="beta3-screenshot" src="https://github.com/user-attachments/assets/c060ba3b-2927-4380-8c58-a34cc4c424a2" />

## 主な機能

- **マルチカメラ撮影** — USB ウェブカメラ、GenICam 対応の産業用カメラ（Aravis）、Teledyne FLIR のハイスピードカメラ（Spinnaker）に対応し、インパクトを接写する専用カメラも使えます。
- **スマートフォンをカメラに** — PinPoint Capture を実行している iPhone が、WiFi または USB ケーブル経由の暗号化された接続でセッションに参加します。
- **マーカーレスのスイングトラッキング** — 全身の姿勢推定（ViTPose）、テープやマーカーを使わないシャフトとクラブヘッドのトラッキング、ボール検出。
- **ショットの自動検出** — IMU が捉えたインパクト、打球音、または弾道測定器からショットを検出し、1 球ごとにその場で解析して保存します。
- **スイング指標** — 10 の P-positions（アドレスからフィニッシュまで）、テンポ、クラブデリバリー、体の回転、姿勢、手首の角度、キネマティックシーケンス（運動連鎖）を、それぞれの信頼度とともに表示します。
- **診断** — スイングの欠点と長所を基準範囲に照らして特定し、考えられる原因とドリルを、ショットごと、およびセッション全体について示します。
- **手首の解析** — Witmotion の IMU、または HackMotion wG3 センサーで、スイング中のリード側の手首の掌屈、背屈、コックを捉えます。
- **弾道測定器** — Foresight GCQuad（FSX2020 経由）に加え、GSPro Open Connect ブリッジのある機器（Garmin R10、Rapsodo MLM2PRO、SkyTrak+ など）ならどれでも使えます。計測値は、そのショットを打ったスイングと対応付けられます。
- **レビュー** — すべてのカメラ映像を同期再生し、オーバーレイ、グラフ、フェーズのタイムラインとあわせて確認できます。すべてのセッションをディスクに残すスイングライブラリも備えています。
- **正解データのアノテーション** — アプリ内で P-positions をフレーム単位の精度でラベル付けでき、解析の検証と調整に使えます。
- **音声** — ローカルで動作する音声認識（whisper.cpp）と音声合成（Kokoro）。必要に応じてクラウドへのフォールバックも設定できます。
- **ローカルファーストでプライバシー重視** — 解析は手元のマシン上で実行され、利用できる場合は GPU アクセラレーション（CUDA、Vulkan、CoreML、Metal）を使います。テレメトリはなく、クラウドサービスは自分で設定しないかぎり無効です。
- **アプリ内アップデート** — Windows と macOS 向けには、その場で自身を更新する署名付きインストーラーを用意しています。自己更新に対応した Linux 用 AppImage は準備中で、それまで Linux ではソースからビルドします。

## 入手方法

インストーラーは [Releases ページ](https://github.com/PinPoint-Golf/PinPointStudio/releases) で公開しています。ソースからビルドする場合は **[BUILDING.md](BUILDING.md)** を参照してください。

## リポジトリの構成

| パス | 内容 |
|---|---|
| `src/` | アプリケーション本体。サブシステムごとに 1 フォルダー（下記参照） |
| `tests/` | テスト全体をまとめるビルド。各テストスイートは対象コードのそばの `src/<area>/tests/` にあります |
| `tools/` | オフラインツールと実験用ツール: SwingLab（記録済みのスイングに対して解析パイプラインを再実行）、シャフト / ボール / インパクトの各ラボ、弾道測定器のフェイク実装、パッケージング用スクリプト |
| `packaging/` | Windows 用インストーラーと appcast のスクリプト、macOS 向けのパッケージング用アセット |
| `cmake/` | ビルドモジュール: Qt の検出、QML モジュール、フォント、OpenSSL、コード署名 |
| `third_party/` | 同梱している小規模な外部ソース（ESKF 姿勢推定フィルター） |
| `docs/` | ドキュメント。読者別に整理しています（下記参照） |

`src/` の内訳:

| フォルダー | サブシステム |
|---|---|
| `Gui/` | Qt Quick / QML のインターフェースと、その C++ モデル |
| `Video/`, `Pose/` | カメラバックエンド。姿勢推定、ボール検出、人物セグメンテーションの各モデル |
| `Analysis/`, `Metrics/`, `Diagnostics/` | スイング解析パイプライン、指標カタログ、スイングの欠点の診断 |
| `Buffer/` | すべてのセンサーの時刻を揃えるロックフリーのイベントバッファー |
| `IMU/`, `LaunchMonitor/`, `Ppcp/`, `Audio/` | センサー、弾道測定器、スマートフォンキャプチャ、マイクと打球音 |
| `Export/` | スイング動画と `swing.json` ドキュメント |
| `STT/`, `TTS/`, `LLM/`, `Secrets/` | 音声、AI コーチ、API キーの保管 |
| `Update/`, `Core/`, `Resources/`, `Shaders/` | アプリ内アップデート、共通基盤、同梱アセット、GPU シェーダー |

## ドキュメント

| 場所 | 内容 |
|---|---|
| [BUILDING.md](BUILDING.md) | 依存関係、各プラットフォームでのビルド、パッケージング、テストの実行 |
| [`docs/user/`](docs/user) | アプリの使い方: [診断ガイド](docs/user/pinpoint-diagnostics-guide.md)、[手首のキャリブレーション](docs/user/wristcalibration.md)、[スマートフォンキャプチャ](docs/user/phone_capture.md)、UX デザイン |
| [`docs/developer/`](docs/developer) | 開発者向けガイド: [解析パイプライン](docs/developer/analysis_pipeline_developer_guide.md)、[テスト](docs/developer/testing_developer_guide.md)、[SwingLab](docs/developer/swinglab_developer_guide.md)、[機能スイッチ](docs/developer/feature_switches_developer_guide.md)、[翻訳](docs/developer/translations_developer_guide.md)、イベントバッファー、診断、指標 |
| [`docs/design/`](docs/design) | 各サブシステムの設計記録と、[QML デザインシステム](docs/design/pinpoint_qml_design_system.md) |
| [`docs/reference/`](docs/reference) | ファイル形式（[`swing.json`](docs/reference/swing_json_schema.md)、[スイングフォルダー](docs/reference/swing_folder_layout.md)）、符号の規約、デバイスプロトコル、基準データ |
| [`docs/validation/`](docs/validation), [`docs/research/`](docs/research) | 検証プロトコル、コーパスでの結果、研究ノート |
| `docs/implementation/` | 作業計画とリリース手順書 |

## 関連プロジェクト

以下の姉妹リポジトリを依存関係として利用しています。

- [libppcp](https://github.com/PinPoint-Golf/libppcp) — PinPoint Capture Protocol。スマートフォンアプリと共有しています
- [PinPoint Capture](https://github.com/PinPoint-Golf/PinPointCapture) — iPhone 用のキャプチャアプリ
- [libwrist](https://github.com/PinPoint-Golf/libwrist) — HackMotion wG3 センサーのドライバー
- [libgspro](https://github.com/PinPoint-Golf/libgspro) — GSPro Open Connect サーバー

## ライセンス

PinPoint Studio は、GNU General Public License バージョン 2 またはそれ以降のバージョンのもとでライセンスされています。配布されるバイナリは、リンクしているコンポーネントの都合により GPL-3.0 のもとで提供されます。[LICENSE](LICENSE) を、またすべてのサードパーティコンポーネントとその条件については [LICENSEDEPS.md](LICENSEDEPS.md) を参照してください。
