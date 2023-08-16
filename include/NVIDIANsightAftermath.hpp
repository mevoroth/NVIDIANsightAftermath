#pragma once

#include "NsightAftermathGpuCrashTracker.h"

struct GFSDK_Aftermath_ContextHandle__;
struct ID3D12Device;
struct ID3D12CommandList;

namespace NVIDIA
{
	using UINT64 = unsigned long long;
	using HRESULT = long;

	class NVIDIANsightAftermathContext;

	class NVIDIANsightAftermath
	{
	public:
		NVIDIANsightAftermath();

		void InitializeGpuCrashTracker();
		void InitializeAftermath(ID3D12Device* const InD3D12Device);
		void OnPresent(HRESULT InHResult);
		void ResetMarkers();
		void SetEventMarker(NVIDIANsightAftermathContext& InOutAftermathContext, const char* InEventName);

	private:
		// App-managed marker functionality
		UINT64								_FrameCounter = 0ull;
		GpuCrashTracker::MarkerMap			_MarkerMap;

		// Nsight Aftermath instrumentation
		GpuCrashTracker						_GpuCrashTracker;
	};

	class NVIDIANsightAftermathContext
	{
	public:

		void InitializeAftermathContext(ID3D12CommandList* InCommandList);
		void ReleaseAftermathContext();
		GFSDK_Aftermath_ContextHandle__* GetAftermathCommandListContext() { return _AftermathCommandListContext; }

	private:
		GFSDK_Aftermath_ContextHandle__* _AftermathCommandListContext = nullptr;
	};
}
