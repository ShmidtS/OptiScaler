#include "pch.h"
#include "D3D11ShaderResourceBackup.h"

D3D11ShaderResourceBackup::D3D11ShaderResourceBackup()
{
    memset(_csSRVs, 0, sizeof(_csSRVs));
    memset(_csUAVs, 0, sizeof(_csUAVs));
    memset(_csSamplers, 0, sizeof(_csSamplers));
    memset(_csCBVs, 0, sizeof(_csCBVs));

    memset(_vsSRVs, 0, sizeof(_vsSRVs));
    memset(_vsSamplers, 0, sizeof(_vsSamplers));
    memset(_vsCBVs, 0, sizeof(_vsCBVs));

    memset(_psSRVs, 0, sizeof(_psSRVs));
    memset(_psSamplers, 0, sizeof(_psSamplers));
    memset(_psCBVs, 0, sizeof(_psCBVs));

    memset(_gsSRVs, 0, sizeof(_gsSRVs));
    memset(_gsSamplers, 0, sizeof(_gsSamplers));
    memset(_gsCBVs, 0, sizeof(_gsCBVs));

    memset(_hsSRVs, 0, sizeof(_hsSRVs));
    memset(_hsSamplers, 0, sizeof(_hsSamplers));
    memset(_hsCBVs, 0, sizeof(_hsCBVs));

    memset(_dsSRVs, 0, sizeof(_dsSRVs));
    memset(_dsSamplers, 0, sizeof(_dsSamplers));
    memset(_dsCBVs, 0, sizeof(_dsCBVs));

    memset(_rtvs, 0, sizeof(_rtvs));
}

D3D11ShaderResourceBackup::~D3D11ShaderResourceBackup()
{
    ReleaseAll();
}

D3D11ShaderResourceBackup::D3D11ShaderResourceBackup(D3D11ShaderResourceBackup&& other) noexcept
    : _hasBackup(other._hasBackup), _flags(other._flags)
{
    memcpy(_csSRVs, other._csSRVs, sizeof(_csSRVs));
    memcpy(_csUAVs, other._csUAVs, sizeof(_csUAVs));
    memcpy(_csSamplers, other._csSamplers, sizeof(_csSamplers));
    memcpy(_csCBVs, other._csCBVs, sizeof(_csCBVs));

    memcpy(_vsSRVs, other._vsSRVs, sizeof(_vsSRVs));
    memcpy(_vsSamplers, other._vsSamplers, sizeof(_vsSamplers));
    memcpy(_vsCBVs, other._vsCBVs, sizeof(_vsCBVs));

    memcpy(_psSRVs, other._psSRVs, sizeof(_psSRVs));
    memcpy(_psSamplers, other._psSamplers, sizeof(_psSamplers));
    memcpy(_psCBVs, other._psCBVs, sizeof(_psCBVs));

    memcpy(_gsSRVs, other._gsSRVs, sizeof(_gsSRVs));
    memcpy(_gsSamplers, other._gsSamplers, sizeof(_gsSamplers));
    memcpy(_gsCBVs, other._gsCBVs, sizeof(_gsCBVs));

    memcpy(_hsSRVs, other._hsSRVs, sizeof(_hsSRVs));
    memcpy(_hsSamplers, other._hsSamplers, sizeof(_hsSamplers));
    memcpy(_hsCBVs, other._hsCBVs, sizeof(_hsCBVs));

    memcpy(_dsSRVs, other._dsSRVs, sizeof(_dsSRVs));
    memcpy(_dsSamplers, other._dsSamplers, sizeof(_dsSamplers));
    memcpy(_dsCBVs, other._dsCBVs, sizeof(_dsCBVs));

    memcpy(_rtvs, other._rtvs, sizeof(_rtvs));
    _dsv = other._dsv;

    // Clear other without releasing
    memset(other._csSRVs, 0, sizeof(other._csSRVs));
    memset(other._csUAVs, 0, sizeof(other._csUAVs));
    memset(other._csSamplers, 0, sizeof(other._csSamplers));
    memset(other._csCBVs, 0, sizeof(other._csCBVs));
    memset(other._vsSRVs, 0, sizeof(other._vsSRVs));
    memset(other._vsSamplers, 0, sizeof(other._vsSamplers));
    memset(other._vsCBVs, 0, sizeof(other._vsCBVs));
    memset(other._psSRVs, 0, sizeof(other._psSRVs));
    memset(other._psSamplers, 0, sizeof(other._psSamplers));
    memset(other._psCBVs, 0, sizeof(other._psCBVs));
    memset(other._gsSRVs, 0, sizeof(other._gsSRVs));
    memset(other._gsSamplers, 0, sizeof(other._gsSamplers));
    memset(other._gsCBVs, 0, sizeof(other._gsCBVs));
    memset(other._hsSRVs, 0, sizeof(other._hsSRVs));
    memset(other._hsSamplers, 0, sizeof(other._hsSamplers));
    memset(other._hsCBVs, 0, sizeof(other._hsCBVs));
    memset(other._dsSRVs, 0, sizeof(other._dsSRVs));
    memset(other._dsSamplers, 0, sizeof(other._dsSamplers));
    memset(other._dsCBVs, 0, sizeof(other._dsCBVs));
    memset(other._rtvs, 0, sizeof(other._rtvs));
    other._dsv = nullptr;
    other._hasBackup = false;
}

D3D11ShaderResourceBackup& D3D11ShaderResourceBackup::operator=(D3D11ShaderResourceBackup&& other) noexcept
{
    if (this != &other)
    {
        ReleaseAll();

        memcpy(_csSRVs, other._csSRVs, sizeof(_csSRVs));
        memcpy(_csUAVs, other._csUAVs, sizeof(_csUAVs));
        memcpy(_csSamplers, other._csSamplers, sizeof(_csSamplers));
        memcpy(_csCBVs, other._csCBVs, sizeof(_csCBVs));

        memcpy(_vsSRVs, other._vsSRVs, sizeof(_vsSRVs));
        memcpy(_vsSamplers, other._vsSamplers, sizeof(_vsSamplers));
        memcpy(_vsCBVs, other._vsCBVs, sizeof(_vsCBVs));

        memcpy(_psSRVs, other._psSRVs, sizeof(_psSRVs));
        memcpy(_psSamplers, other._psSamplers, sizeof(_psSamplers));
        memcpy(_psCBVs, other._psCBVs, sizeof(_psCBVs));

        memcpy(_gsSRVs, other._gsSRVs, sizeof(_gsSRVs));
        memcpy(_gsSamplers, other._gsSamplers, sizeof(_gsSamplers));
        memcpy(_gsCBVs, other._gsCBVs, sizeof(_gsCBVs));

        memcpy(_hsSRVs, other._hsSRVs, sizeof(_hsSRVs));
        memcpy(_hsSamplers, other._hsSamplers, sizeof(_hsSamplers));
        memcpy(_hsCBVs, other._hsCBVs, sizeof(_hsCBVs));

        memcpy(_dsSRVs, other._dsSRVs, sizeof(_dsSRVs));
        memcpy(_dsSamplers, other._dsSamplers, sizeof(_dsSamplers));
        memcpy(_dsCBVs, other._dsCBVs, sizeof(_dsCBVs));

        memcpy(_rtvs, other._rtvs, sizeof(_rtvs));
        _dsv = other._dsv;

        _hasBackup = other._hasBackup;
        _flags = other._flags;

        // Clear other without releasing
        memset(other._csSRVs, 0, sizeof(other._csSRVs));
        memset(other._csUAVs, 0, sizeof(other._csUAVs));
        memset(other._csSamplers, 0, sizeof(other._csSamplers));
        memset(other._csCBVs, 0, sizeof(other._csCBVs));
        memset(other._vsSRVs, 0, sizeof(other._vsSRVs));
        memset(other._vsSamplers, 0, sizeof(other._vsSamplers));
        memset(other._vsCBVs, 0, sizeof(other._vsCBVs));
        memset(other._psSRVs, 0, sizeof(other._psSRVs));
        memset(other._psSamplers, 0, sizeof(other._psSamplers));
        memset(other._psCBVs, 0, sizeof(other._psCBVs));
        memset(other._gsSRVs, 0, sizeof(other._gsSRVs));
        memset(other._gsSamplers, 0, sizeof(other._gsSamplers));
        memset(other._gsCBVs, 0, sizeof(other._gsCBVs));
        memset(other._hsSRVs, 0, sizeof(other._hsSRVs));
        memset(other._hsSamplers, 0, sizeof(other._hsSamplers));
        memset(other._hsCBVs, 0, sizeof(other._hsCBVs));
        memset(other._dsSRVs, 0, sizeof(other._dsSRVs));
        memset(other._dsSamplers, 0, sizeof(other._dsSamplers));
        memset(other._dsCBVs, 0, sizeof(other._dsCBVs));
        memset(other._rtvs, 0, sizeof(other._rtvs));
        other._dsv = nullptr;
        other._hasBackup = false;
    }
    return *this;
}

void D3D11ShaderResourceBackup::Backup(ID3D11DeviceContext* context)
{
    if (context == nullptr)
        return;

    // Clear previous backup first
    ReleaseAll();

    // Compute Shader resources
    if (_flags.computeShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            context->CSGetShaderResources(i, 1, &_csSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            context->CSGetSamplers(i, 1, &_csSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            context->CSGetConstantBuffers(i, 1, &_csCBVs[i]);
        }
        for (UINT i = 0; i < UAV_SLOT_COUNT; i++)
        {
            context->CSGetUnorderedAccessViews(i, 1, &_csUAVs[i]);
        }
    }

    // Vertex Shader resources
    if (_flags.vertexShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            context->VSGetShaderResources(i, 1, &_vsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            context->VSGetSamplers(i, 1, &_vsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            context->VSGetConstantBuffers(i, 1, &_vsCBVs[i]);
        }
    }

    // Pixel Shader resources
    if (_flags.pixelShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            context->PSGetShaderResources(i, 1, &_psSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            context->PSGetSamplers(i, 1, &_psSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            context->PSGetConstantBuffers(i, 1, &_psCBVs[i]);
        }
    }

    // Geometry Shader resources
    if (_flags.geometryShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            context->GSGetShaderResources(i, 1, &_gsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            context->GSGetSamplers(i, 1, &_gsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            context->GSGetConstantBuffers(i, 1, &_gsCBVs[i]);
        }
    }

    // Hull Shader resources
    if (_flags.hullShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            context->HSGetShaderResources(i, 1, &_hsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            context->HSGetSamplers(i, 1, &_hsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            context->HSGetConstantBuffers(i, 1, &_hsCBVs[i]);
        }
    }

    // Domain Shader resources
    if (_flags.domainShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            context->DSGetShaderResources(i, 1, &_dsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            context->DSGetSamplers(i, 1, &_dsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            context->DSGetConstantBuffers(i, 1, &_dsCBVs[i]);
        }
    }

    // Render targets
    if (_flags.renderTargets)
    {
        context->OMGetRenderTargets(RTV_SLOT_COUNT, _rtvs, &_dsv);
    }

    _hasBackup = true;
}

void D3D11ShaderResourceBackup::Restore(ID3D11DeviceContext* context)
{
    if (context == nullptr || !_hasBackup)
        return;

    // Compute Shader resources
    if (_flags.computeShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            if (_csSRVs[i] != nullptr)
                context->CSSetShaderResources(i, 1, &_csSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            if (_csSamplers[i] != nullptr)
                context->CSSetSamplers(i, 1, &_csSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            if (_csCBVs[i] != nullptr)
                context->CSSetConstantBuffers(i, 1, &_csCBVs[i]);
        }
        for (UINT i = 0; i < UAV_SLOT_COUNT; i++)
        {
            if (_csUAVs[i] != nullptr)
                context->CSSetUnorderedAccessViews(i, 1, &_csUAVs[i], 0);
        }
    }

    // Vertex Shader resources
    if (_flags.vertexShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            if (_vsSRVs[i] != nullptr)
                context->VSSetShaderResources(i, 1, &_vsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            if (_vsSamplers[i] != nullptr)
                context->VSSetSamplers(i, 1, &_vsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            if (_vsCBVs[i] != nullptr)
                context->VSSetConstantBuffers(i, 1, &_vsCBVs[i]);
        }
    }

    // Pixel Shader resources
    if (_flags.pixelShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            if (_psSRVs[i] != nullptr)
                context->PSSetShaderResources(i, 1, &_psSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            if (_psSamplers[i] != nullptr)
                context->PSSetSamplers(i, 1, &_psSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            if (_psCBVs[i] != nullptr)
                context->PSSetConstantBuffers(i, 1, &_psCBVs[i]);
        }
    }

    // Geometry Shader resources
    if (_flags.geometryShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            if (_gsSRVs[i] != nullptr)
                context->GSSetShaderResources(i, 1, &_gsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            if (_gsSamplers[i] != nullptr)
                context->GSSetSamplers(i, 1, &_gsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            if (_gsCBVs[i] != nullptr)
                context->GSSetConstantBuffers(i, 1, &_gsCBVs[i]);
        }
    }

    // Hull Shader resources
    if (_flags.hullShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            if (_hsSRVs[i] != nullptr)
                context->HSSetShaderResources(i, 1, &_hsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            if (_hsSamplers[i] != nullptr)
                context->HSSetSamplers(i, 1, &_hsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            if (_hsCBVs[i] != nullptr)
                context->HSSetConstantBuffers(i, 1, &_hsCBVs[i]);
        }
    }

    // Domain Shader resources
    if (_flags.domainShader)
    {
        for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
        {
            if (_dsSRVs[i] != nullptr)
                context->DSSetShaderResources(i, 1, &_dsSRVs[i]);
        }
        for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
        {
            if (_dsSamplers[i] != nullptr)
                context->DSSetSamplers(i, 1, &_dsSamplers[i]);
        }
        for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
        {
            if (_dsCBVs[i] != nullptr)
                context->DSSetConstantBuffers(i, 1, &_dsCBVs[i]);
        }
    }

    // Render targets
    if (_flags.renderTargets)
    {
        context->OMSetRenderTargets(RTV_SLOT_COUNT, _rtvs, _dsv);
    }
}

void D3D11ShaderResourceBackup::Clear()
{
    ReleaseAll();
    _hasBackup = false;
}

void D3D11ShaderResourceBackup::ReleaseAll()
{
    // Release Compute Shader resources
    for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
    {
        if (_csSRVs[i])
        {
            _csSRVs[i]->Release();
            _csSRVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < UAV_SLOT_COUNT; i++)
    {
        if (_csUAVs[i])
        {
            _csUAVs[i]->Release();
            _csUAVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
    {
        if (_csSamplers[i])
        {
            _csSamplers[i]->Release();
            _csSamplers[i] = nullptr;
        }
    }
    for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
    {
        if (_csCBVs[i])
        {
            _csCBVs[i]->Release();
            _csCBVs[i] = nullptr;
        }
    }

    // Release Vertex Shader resources
    for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
    {
        if (_vsSRVs[i])
        {
            _vsSRVs[i]->Release();
            _vsSRVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
    {
        if (_vsSamplers[i])
        {
            _vsSamplers[i]->Release();
            _vsSamplers[i] = nullptr;
        }
    }
    for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
    {
        if (_vsCBVs[i])
        {
            _vsCBVs[i]->Release();
            _vsCBVs[i] = nullptr;
        }
    }

    // Release Pixel Shader resources
    for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
    {
        if (_psSRVs[i])
        {
            _psSRVs[i]->Release();
            _psSRVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
    {
        if (_psSamplers[i])
        {
            _psSamplers[i]->Release();
            _psSamplers[i] = nullptr;
        }
    }
    for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
    {
        if (_psCBVs[i])
        {
            _psCBVs[i]->Release();
            _psCBVs[i] = nullptr;
        }
    }

    // Release Geometry Shader resources
    for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
    {
        if (_gsSRVs[i])
        {
            _gsSRVs[i]->Release();
            _gsSRVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
    {
        if (_gsSamplers[i])
        {
            _gsSamplers[i]->Release();
            _gsSamplers[i] = nullptr;
        }
    }
    for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
    {
        if (_gsCBVs[i])
        {
            _gsCBVs[i]->Release();
            _gsCBVs[i] = nullptr;
        }
    }

    // Release Hull Shader resources
    for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
    {
        if (_hsSRVs[i])
        {
            _hsSRVs[i]->Release();
            _hsSRVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
    {
        if (_hsSamplers[i])
        {
            _hsSamplers[i]->Release();
            _hsSamplers[i] = nullptr;
        }
    }
    for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
    {
        if (_hsCBVs[i])
        {
            _hsCBVs[i]->Release();
            _hsCBVs[i] = nullptr;
        }
    }

    // Release Domain Shader resources
    for (UINT i = 0; i < SRV_SLOT_COUNT; i++)
    {
        if (_dsSRVs[i])
        {
            _dsSRVs[i]->Release();
            _dsSRVs[i] = nullptr;
        }
    }
    for (UINT i = 0; i < SAMPLER_SLOT_COUNT; i++)
    {
        if (_dsSamplers[i])
        {
            _dsSamplers[i]->Release();
            _dsSamplers[i] = nullptr;
        }
    }
    for (UINT i = 0; i < CBV_SLOT_COUNT; i++)
    {
        if (_dsCBVs[i])
        {
            _dsCBVs[i]->Release();
            _dsCBVs[i] = nullptr;
        }
    }

    // Release Render Target resources
    for (UINT i = 0; i < RTV_SLOT_COUNT; i++)
    {
        if (_rtvs[i])
        {
            _rtvs[i]->Release();
            _rtvs[i] = nullptr;
        }
    }
    if (_dsv)
    {
        _dsv->Release();
        _dsv = nullptr;
    }
}