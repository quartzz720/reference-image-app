# Reference Image App

Minimal always-on-top image viewer for Windows. Useful for artists, designers, and animators who need a reference image pinned above other apps; resizing keeps the original aspect ratio.

## Build

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable will be at `build/Release/reference-app.exe`.

## Run

Launch the exe and pick an image in the file dialog.
