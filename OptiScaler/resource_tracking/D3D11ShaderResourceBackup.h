#pragma once

#include <d3d11.h>

// Safely saves and restores D3D11 shader resources (SRV, UAV, Samplers, CBV) for all shader stages
class D3D11ShaderResourceBackup
{
  public:
    D3D11ShaderResourceBackup();
    ~D3D11ShaderResourceBackup();

    // Non-copyable, movable
    D3D11ShaderResourceBackup(const D3D11ShaderResourceBackup&) = delete;
    D3D11ShaderResourceBackup& operator=(const D3D11ShaderResourceBackup&) = delete;
    D3D11ShaderResourceBackup(D3D11ShaderResourceBackup&&) noexcept;
    D3D11ShaderResourceBackup& operator=(D3D11ShaderResourceBackup&&) noexcept;

    // Backup current bound resources from the device context
    void Backup(ID3D11DeviceContext* context);

    // Restore previously backed up resources to the device context
    void Restore(ID3D11DeviceContext* context);

    // Clear all stored resources (release references)
    void Clear();

    // Check if backup contains valid data
    bool HasBackup() const { return _hasBackup; }

    // Backup options
    struct BackupFlags
    {
        bool computeShader = true;     // CS resources (SRV, UAV, Sampler, CBV)
        bool pixelShader = true;       // PS resources (SRV, Sampler, CBV)
        bool vertexShader = true;      // VS resources (SRV, Sampler, CBV)
        bool geometryShader = false;   // GS resources (SRV, Sampler, CBV)
        bool hullShader = false;       // HS resources (SRV, Sampler, CBV)
        bool domainShader = false;     // DS resources (SRV, Sampler, CBV)
        bool renderTargets = true;     // RTV and DSV
    };

    void SetBackupFlags(const BackupFlags& flags) { _flags = flags; }
    const BackupFlags& GetBackupFlags() const { return _flags; }

  private:
    void ReleaseAll();

    // Constant buffer counts
    static constexpr UINT SRV_SLOT_COUNT = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    static constexpr UINT SAMPLER_SLOT_COUNT = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
    static constexpr UINT CBV_SLOT_COUNT = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
    static constexpr UINT UAV_SLOT_COUNT = D3D11_1_UAV_SLOT_COUNT;
    static constexpr UINT RTV_SLOT_COUNT = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

    // Compute Shader resources
    ID3D11ShaderResourceView* _csSRVs[SRV_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* _csUAVs[UAV_SLOT_COUNT] = {};
    ID3D11SamplerState* _csSamplers[SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* _csCBVs[CBV_SLOT_COUNT] = {};

    // Vertex Shader resources
    ID3D11ShaderResourceView* _vsSRVs[SRV_SLOT_COUNT] = {};
    ID3D11SamplerState* _vsSamplers[SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* _vsCBVs[CBV_SLOT_COUNT] = {};

    // Pixel Shader resources
    ID3D11ShaderResourceView* _psSRVs[SRV_SLOT_COUNT] = {};
    ID3D11SamplerState* _psSamplers[SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* _psCBVs[CBV_SLOT_COUNT] = {};

    // Geometry Shader resources
    ID3D11ShaderResourceView* _gsSRVs[SRV_SLOT_COUNT] = {};
    ID3D11SamplerState* _gsSamplers[SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* _gsCBVs[CBV_SLOT_COUNT] = {};

    // Hull Shader resources
    ID3D11ShaderResourceView* _hsSRVs[SRV_SLOT_COUNT] = {};
    ID3D11SamplerState* _hsSamplers[SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* _hsCBVs[CBV_SLOT_COUNT] = {};

    // Domain Shader resources
    ID3D11ShaderResourceView* _dsSRVs[SRV_SLOT_COUNT] = {};
    ID3D11SamplerState* _dsSamplers[SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* _dsCBVs[CBV_SLOT_COUNT] = {};

    // Output Merger resources
    ID3D11RenderTargetView* _rtvs[RTV_SLOT_COUNT] = {};
    ID3D11DepthStencilView* _dsv = nullptr;

    bool _hasBackup = false;
    BackupFlags _flags;
};
