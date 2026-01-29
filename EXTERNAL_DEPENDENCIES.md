# External Dependencies Configuration

This file documents the configuration for external dependencies in OptiScaler.

## Submodule Branch Configuration

The following submodules are configured to use specific default branches when the repository is cloned recursively:

- `external/FidelityFX-SDK`: Uses `main` branch
- `external/dlssg-to-fsr3`: Uses `main` branch
- `external/simpleini`: Uses `main` branch
- `external/unordered_dense`: Uses `main` branch
- `external/xess`: Uses `main` branch
- `external/vulkan`: Uses `main` branch
- `external/spdlog`: Uses `main` branch
- `external/magic_enum`: Uses `main` branch

## Tracked Files

The following specific files are tracked in the main repository (not in submodules):

- `external/FidelityFX-SDK/Kits/FidelityFX/api/include/vk/ffx_api_vk.h`
- `external/FidelityFX-SDK/Kits/FidelityFX/api/include/vk/ffx_api_vk.hpp`
- `external/dlssg-to-fsr3/source/maindll/NGX/NvNGX.h`

## Setup Instructions

To properly set up the external dependencies:

1. Clone the repository recursively:
   ```bash
   git clone --recursive https://github.com/ShmidtS/OptiScaler.git
   cd OptiScaler
   ```

2. Update submodules to use the configured branches:
   ```bash
   git submodule update --init --recursive
   ```

3. The tracked files will be available in their respective paths

## File Tracking Strategy

Since these files are in submodules, they are tracked separately in the main repository to ensure they are always available when the project is cloned. This approach:
- Ensures the files are always present when cloning recursively
- Allows for independent versioning of the external dependencies
- Provides a stable interface for the OptiScaler project