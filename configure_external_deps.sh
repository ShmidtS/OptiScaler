#!/bin/bash

# Script to configure external dependencies to track specific files
# and set up proper default branches

echo "Configuring external dependencies..."

# Initialize and update submodules with specific branch configurations
echo "Updating submodules with branch configurations..."

# Remove existing submodules to reconfigure
git submodule deinit -f external/FidelityFX-SDK
git submodule deinit -f external/dlssg-to-fsr3
rm -rf .git/modules/external/FidelityFX-SDK
rm -rf .git/modules/external/dlssg-to-fsr3

# Initialize submodules with the new branch configurations
git submodule update --init --recursive --remote

# Configure FidelityFX-SDK to track specific files
echo "Configuring FidelityFX-SDK submodule..."
cd external/FidelityFX-SDK
git checkout main
git submodule update --init --recursive

# Create a separate tracking branch for the specific files
git checkout -b optiscaler-tracked-files main
git add Kits/FidelityFX/api/include/vk/ffx_api_vk.h Kits/FidelityFX/api/include/vk/ffx_api_vk.hpp
git commit -m "Track specific FidelityFX API files for OptiScaler"
cd ..

# Configure dlssg-to-fsr3 submodule
echo "Configuring dlssg-to-fsr3 submodule..."
cd external/dlssg-to-fsr3
git checkout main
git submodule update --init --recursive

# Create a separate tracking branch for the specific files
git checkout -b optiscaler-tracked-files main
git add source/maindll/NGX/NvNGX.h
git commit -m "Track NvNGX.h for OptiScaler"
cd ..

echo "External dependencies configured successfully!"
echo ""
echo "To use these configurations:"
echo "1. git submodule update --init --recursive"
echo "2. The specific files will now be tracked in the main repository"