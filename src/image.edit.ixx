module;

#include "sof/sof.h"
#include <span>

export module sof.image.edit;
import sof.image.load;
export namespace sof {

	// A skillset for the ImageLoader class template, this can be installed into the image loaders statically
	// this provides access to higher level data structures of the PE-COFF file such as functions, sections
	// and more in the future, as this class will be used as the image editor to allow the virtualizer
	// to write to the image in an abstracted way.
	export template<typename T>
	class ImageEditor
		: public CrtpHelp<T> {
	public:
	#pragma region Exception directory parser and editor components
		std::span<const RUNTIME_FUNCTION>
			GetRuntimeFunctionTable() const {
			TRACE_FUNCTION_PROTO;

			// Locate the runtime function table and test, then return view
			auto& Underlying = this->GetUnderlyingCrtpBase();
			IMAGE_DATA_DIRECTORY& ExceptionDirectroy = Underlying.GetImageDataSection(IMAGE_DIRECTORY_ENTRY_EXCEPTION);

			return std::span(
				reinterpret_cast<const PRUNTIME_FUNCTION>(
					ExceptionDirectroy.VirtualAddress
					+ Underlying.GetImageFileMapping()),
				ExceptionDirectroy.Size / sizeof(RUNTIME_FUNCTION));
		}
		PRUNTIME_FUNCTION GetRuntimeFunctionForAddress(
			IN rva_t RvaWithinPossibleFunction
		) {
			TRACE_FUNCTION_PROTO;
			auto ViewOfFunctionTable = GetRuntimeFunctionTable();
			for (auto& FunctionEntry : ViewOfFunctionTable)
				if (FunctionEntry.BeginAddress <= RvaWithinPossibleFunction &&
					FunctionEntry.EndAddress >= RvaWithinPossibleFunction)
					return &FunctionEntry;
			return nullptr;
		}
		PRUNTIME_FUNCTION GetRuntimeFunctionForAddress(
			IN byte_t* VirtualAddressWithinPossibleFunction
		) {
			TRACE_FUNCTION_PROTO;
			return GetRuntimeFunctionForAddress(
				reinterpret_cast<rva_t>(
					VirtualAddressWithinPossibleFunction
					- this->GetUnderlyingCrtpBase().GetImageFileMapping()));
		}
	#pragma endregion

		uint32_t ApproximateNumberOfRelocationsInImage() const { // Calculates a rough estimate of the number of relocations in the image,
																 // by taking the average of the higher and lower bounds.
			TRACE_FUNCTION_PROTO;

			const T& Underlying = this->GetUnderlyingCrtpBase();
			auto& RelocationDirectory = Underlying.GetImageDataSection(IMAGE_DIRECTORY_ENTRY_BASERELOC);
			if (!RelocationDirectory.Size)
				return 0;

			uint32_t MaxNumberOfRelocations = RelocationDirectory.Size / 2;
			auto SectionHeaders = Underlying.GetImageSectionHeaders();
			size_t SizeOfPrimaryImage = 0;
			for (const auto& Section : SectionHeaders)
				SizeOfPrimaryImage += Section.Misc.VirtualSize;

			uint32_t MinNumberOfRelocations = MaxNumberOfRelocations -
				SizeOfPrimaryImage / 4096;
			return (MaxNumberOfRelocations + MinNumberOfRelocations) / 2;
		}

	};
}
