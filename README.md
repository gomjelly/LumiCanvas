# LumiCanvas

Windows 전용 프로젝션 매핑 앱 (C++20 · Qt 6 · Direct3D 11).

## 빌드

요구 사항: Visual Studio 2022(C++ 워크로드), CMake 3.21+, Qt 6.8 (msvc2022_64)

```bash
cmake --preset msvc
cmake --build --preset debug
```

실행 파일: `build/Debug/LumiCanvas.exe` (빌드 후 windeployqt가 Qt DLL을 옆에 복사합니다)

Qt 경로가 `C:/Qt/6.8.2/msvc2022_64`가 아니면 `CMakeUserPresets.json`에서 `CMAKE_PREFIX_PATH`를 덮어쓰세요.

## 구조

| 경로 | 역할 |
|---|---|
| `src/core/Project` | 데이터 모델(서피스, 코너, 콘텐츠), `.lumi` JSON 저장/불러오기 |
| `src/render/RenderEngine` | D3D11 디바이스 + 렌더 스레드. 출력마다 flip-model 스왑체인 |
| `src/render/OutputWindow` | 프로젝터 출력용 네이티브 창 (전체화면/창 모드) |
| `src/ui/MainWindow` | 편집기 UI (서피스 목록, 속성, 출력 관리) |
| `src/ui/WarpEditor` | 코너 드래그/키보드 미세조정 캔버스 |
| `resources/shaders/warp.hlsl` | 역 호모그래피 기반 원근 보정 쿼드 워프 + 테스트 패턴 |

UI 스레드와 렌더 스레드는 분리되어 있습니다. GUI는 불변 `Scene` 스냅샷만 넘기고, 렌더 스레드가 모든 출력에 그립니다.

## 사용법

- **Output → Open Output**: 선택한 디스플레이에 출력 창을 엽니다. 출력 창에서 `Esc`를 누르면 닫힙니다.
- 편집기에서 코너나 서피스를 드래그합니다. 화살표는 1px, `Shift`+화살표는 10px 이동, `Tab`은 코너 순환입니다.
- `--windowed-output` 옵션으로 실행하면 시작할 때 창 모드 출력이 열립니다 (개발용).
