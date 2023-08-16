#include "NVIDIANsightAftermath.hpp"

#include "NsightAftermathHelpers.h"

namespace NVIDIA
{
	NVIDIANsightAftermath::NVIDIANsightAftermath()
		: _GpuCrashTracker(_MarkerMap)
	{
	}

	void NVIDIANsightAftermath::InitializeGpuCrashTracker()
	{
		_GpuCrashTracker.Initialize();
	}

	void NVIDIANsightAftermath::InitializeAftermath(ID3D12Device* const InD3D12Device)
	{
		const uint32_t AftermathFlags =
			GFSDK_Aftermath_FeatureFlags_EnableMarkers |				// Enable event marker tracking. Only effective in combination with the Nsight Aftermath Crash Dump Monitor.
			GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |		// Enable tracking of resources.
			GFSDK_Aftermath_FeatureFlags_CallStackCapturing |			// Capture call stacks for all draw calls, compute dispatches, and resource copies.
			GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |		// Generate debug information for shaders.
			GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;	// Shader reporting

		AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_Initialize(
			GFSDK_Aftermath_Version_API,
			AftermathFlags,
			InD3D12Device));
	}

	void NVIDIANsightAftermath::OnPresent(HRESULT InHResult)
	{
		if (FAILED(InHResult))
		{
			// DXGI_ERROR error notification is asynchronous to the NVIDIA display
			// driver's GPU crash handling. Give the Nsight Aftermath GPU crash dump
			// thread some time to do its work before terminating the process.
			auto TdrTerminationTimeout = std::chrono::seconds(3);
			auto TStart = std::chrono::steady_clock::now();
			auto TElapsed = std::chrono::milliseconds::zero();

			GFSDK_Aftermath_CrashDump_Status Status = GFSDK_Aftermath_CrashDump_Status_Unknown;
			AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetCrashDumpStatus(&Status));

			while (Status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&
				Status != GFSDK_Aftermath_CrashDump_Status_Finished &&
				TElapsed < TdrTerminationTimeout)
			{
				// Sleep 50ms and poll the status again until timeout or Aftermath finished processing the crash dump.
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetCrashDumpStatus(&Status));

				auto TEnd = std::chrono::steady_clock::now();
				TElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(TEnd - TStart);
			}

			if (Status != GFSDK_Aftermath_CrashDump_Status_Finished)
			{
				std::stringstream Err_msg;
				Err_msg << "Unexpected crash dump status: " << Status;
				MessageBoxA(NULL, Err_msg.str().c_str(), "Aftermath Error", MB_OK);
			}

			// Terminate on failure
			exit(-1);
		}
		_FrameCounter++;
	}

	void NVIDIANsightAftermath::ResetMarkers()
	{
		// clear the marker map for the current frame before writing any markers
		_MarkerMap[_FrameCounter % GpuCrashTracker::c_markerFrameHistory].clear();
	}

	void NVIDIANsightAftermath::SetEventMarker(NVIDIANsightAftermathContext& InOutAftermathContext, const char* InEventName)
	{
		// A helper for setting Aftermath event markers.
		// For maximum CPU performance, use GFSDK_Aftermath_SetEventMarker() with dataSize=0.
		// This instructs Aftermath not to allocate and copy off memory internally, relying on
		// the application to manage marker pointers itself.
		auto SetAftermathEventMarker = [this, &InOutAftermathContext](const std::string& MarkerData, bool AppManagedMarker)
		{
			if (AppManagedMarker)
			{
				// App is responsible for handling marker memory, and for resolving the memory at crash dump generation time.
				// The actual "const void* markerData" passed to Aftermath in this case can be any uniquely identifying value that the app can resolve to the marker data later.
				// For this sample, we will use this approach to generating a unique marker value:
				// We keep a ringbuffer with a marker history of the last c_markerFrameHistory frames (currently 4).
				UINT markerMapIndex = _FrameCounter % GpuCrashTracker::c_markerFrameHistory;
				auto& CurrentFrameMarkerMap = _MarkerMap[markerMapIndex];
				// Take the index into the ringbuffer, multiply by 10000, and add the total number of markers logged so far in the current frame, +1 to avoid a value of zero.
				size_t MarkerID = markerMapIndex * 10000 + CurrentFrameMarkerMap.size() + 1;
				// This value is the unique identifier we will pass to Aftermath and internally associate with the marker data in the map.
				CurrentFrameMarkerMap[MarkerID] = MarkerData;
				AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_SetEventMarker(InOutAftermathContext.GetAftermathCommandListContext(), (void*)MarkerID, 0));
				// For example, if we are on frame 625, markerMapIndex = 625 % 4 = 1...
				// The first marker for the frame will have markerID = 1 * 10000 + 0 + 1 = 10001.
				// The 15th marker for the frame will have markerID = 1 * 10000 + 14 + 1 = 10015.
				// On the next frame, 626, markerMapIndex = 626 % 4 = 2.
				// The first marker for this frame will have markerID = 2 * 10000 + 0 + 1 = 20001.
				// The 15th marker for the frame will have markerID = 2 * 10000 + 14 + 1 = 20015.
				// So with this scheme, we can safely have up to 10000 markers per frame, and can guarantee a unique markerID for each one.
				// There are many ways to generate and track markers and unique marker identifiers!
			}
			else
			{
				AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_SetEventMarker(InOutAftermathContext.GetAftermathCommandListContext(), (void*)MarkerData.c_str(), (unsigned int)MarkerData.size() + 1));
			}
		};
		
		auto CreateMarkerStringForFrame = [this](const char* MarkerString)
		{
			std::stringstream Ss;
			Ss << "Frame " << _FrameCounter << ": " << MarkerString;
			return Ss.str();
		};

		//SetAftermathEventMarker(CreateMarkerStringForFrame(InEventName), false);
	}

	//////////////////////////////////////////////////////////////////////////

	void NVIDIANsightAftermathContext::InitializeAftermathContext(ID3D12CommandList* InCommandList)
	{
		// Create an Nsight Aftermath context handle for setting Aftermath event markers in this command list.
		//AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_CreateContextHandle(InCommandList, &_AftermathCommandListContext));
	}

	void NVIDIANsightAftermathContext::ReleaseAftermathContext()
	{
		//AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_ReleaseContextHandle(_AftermathCommandListContext));
	}
}
