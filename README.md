<img src="assets/icon_preview.png" width="128" height="128" alt="Kestrel Filer icon">

# Kestrel Filer

[![Tests](https://github.com/kurokoji/kestrel/actions/workflows/tests.yml/badge.svg)](https://github.com/kurokoji/kestrel/actions/workflows/tests.yml)

Windows専用・超軽量なデュアルペインファイラー。C++23 + 素のWin32 API / Windows Common Controls のみで実装しており、Electron・Qt・wxWidgets・WinUI・WPF・MFC・.NET などのフレームワークは一切使用していません。

コンセプトは Windows 2000 / Classic UI、CDE/Motif、Midnight Commander のような、密度の高いクラシックなデスクトップアプリです。見た目だけでなく、実装自体も軽量であることを目指しています。

## 特徴

- **起動が速い・依存が少ない**: 単一exe、標準DLLのみ、静的CRTリンク
- **ツリー + 左右2ペイン構成**、ドラッグ可能なスプリッター
- ツリーは遅延読み込み（展開時にのみ子ノードを取得）
- 各ペインに**タブ**（独立した履歴・ソート状態）
- **プレビュー**ペイン（画像はGDI+、動画はシェルのサムネイル、テキストは先頭数KB）
- 右クリックで**Windows標準のシェルコンテキストメニュー**を表示
- ファイル一覧から**外部アプリ（Explorer・ブラウザ等）へのドラッグ&ドロップ**に対応（OLEドラッグソース、CF_HDROP等の標準シェル形式）
- **セッション永続化**（ウィンドウ位置・タブ・スプリッター位置を次回起動時に復元）
- `ReadDirectoryChangesW` による**ディレクトリのライブ監視**（外部からの変更も自動反映）
- **インクリメンタル検索**（現在のディレクトリ内、一致行をハイライト）
- ファイル操作（コピー/移動/削除/リネーム）は `IFileOperation` 経由でシェルに委譲（ごみ箱・進捗UI・競合解決はすべてOS標準）
- ディレクトリ列挙は `std::jthread` でバックグラウンド実行、UIスレッドをブロックしない
- **シングルペイン表示**の切り替え

## ビルド

Visual Studio 2022以降（MSVCツールセット）と CMake が必要です。

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`build.bat` に、VS開発者環境を読み込んでビルドする一連のコマンドをまとめてあります。

生成される `build\kestrel.exe` は単体で動作します。

## キーボードショートカット

| キー | 動作 |
|---|---|
| `Enter` | 開く（フォルダは移動、ファイルは関連付けで開く） |
| `Backspace` | 親ディレクトリへ |
| `Alt+←` / `Alt+→` / `Alt+↑` | 戻る / 進む / 上へ |
| `Tab` | 左右ペインの切り替え |
| `Ctrl+L` | アドレスバーへフォーカス |
| `Ctrl+T` / `Ctrl+W` | 新しいタブ / タブを閉じる |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 次のタブ / 前のタブ |
| `Ctrl+F` | 現在のディレクトリ内を検索 |
| `Ctrl+Shift+T` | ツリーへフォーカス |
| `Ctrl+U` | シングルペイン表示の切り替え |
| `F2` | 名前の変更 |
| `F3` / `F4` | 表示 / 編集 |
| `F5` / `F6` | 反対側のペインへコピー / 移動 |
| `F7` | 新しいフォルダー |
| `F8` / `Delete` | 削除（ごみ箱へ） |
| `Ctrl+C` / `X` / `V` | コピー / 切り取り / 貼り付け |
| マウス戻る/進むボタン | 履歴を戻る / 進む |

## 構成

```
src/
  App.*             起動・COM/GDI+初期化・メッセージループ
  MainWindow.*       メインウィンドウ、メニュー/ツールバー/レイアウト/コマンド処理
  TreePane.*         ディレクトリツリー（遅延読み込み）
  FilePane.*         ファイル一覧ペイン（タブ、検索、ソート、選択）
  PreviewPane.*      プレビュー領域
  DirectoryModel.*   バックグラウンドでのディレクトリ列挙
  DirectoryWatcher.* ReadDirectoryChangesW によるライブ監視
  FileOperations.*   IFileOperation / ShellExecuteEx ラッパー
  IconCache.*        シェルのシステムイメージリストのキャッシュ
  Session.*          セッションの保存・復元（%APPDATA%\Kestrel\session.ini）
  Dialogs.*          軽量な自前ダイアログ（リソース不使用）
```

過剰な抽象化（DI、Event Bus、Service Locatorなど）は避け、必要最小限のクラス構成にしています。
