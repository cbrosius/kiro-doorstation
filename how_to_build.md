# How to Build

This project uses ESP-IDF v5.5.1.

## Prerequisites

Ensure ESP-IDF is installed at: `C:/ESP-IDF/v5.5.1/esp-idf`

## Build Steps (PowerShell)

1.  **Setup Environment**:
    You must load the ESP-IDF environment variables before running `idf.py`.
    ```powershell
    C:/ESP-IDF/v5.5.1/esp-idf/export.ps1
    ```

2.  **Build**:
    ```powershell
    idf.py -B firmware build
    ```

3.  **Flash and Monitor**:
    ```powershell
    idf.py -B firmware flash monitor
    ```

## Common Issues

- **idf.py not found**: Make sure you ran the `export.ps1` script first.
- **Port not found**: Check your USB connection and device manager.
