// Image laoder and reconstructor, used to load, take apart, and recombine PE-Coff files
// 
module; 

#include "VirtualizerBase.h"
#include <memory>
#include <string_view>
#include <bit>
#include <span>
#include <concepts>

export module ImageHelp;


// ImageHelp module exception report model implementation,
// all tools of this toolset throw this type when they encounter a fatal issue
export class ImageHelpException 
	: public CommonExceptionType {
public:
	enum ExceptionCode : UnderlyingType {
		STATUS_FAILED_TO_OPEN_FILE = -2000,
		STATUS_FAILED_TO_CLOSE_FILE,
		STATUS_FAILED_FILE_POINTER,
		STATUS_FAILED_IO_OPERATION,
		STATUS_SIZE_NOT_BIGGER_OR_EQUAL,
		STATUS_FAILED_SIGNATURE_TEST,
		STATUS_IMAGEHELP_REFUSE_WORK,
		STATUS_FAILED_TO_CREATE_MAP,
		STATUS_INVALID_MEMORY_ADDRESS,
		STATUS_IMAGE_NOT_SUITABLE,
		STATUS_UNSUPPORTED_RELOCATION,

		STATUS_INVALID_STATUS = 0
	};

	ImageHelpException(
		IN const std::string_view& ExceptionText,
		IN       ExceptionCode     StatusCode
	)
		: CommonExceptionType(ExceptionText,
			StatusCode,
			CommonExceptionType::EXCEPTION_IMAGE_HELP) {
		TRACE_FUNCTION_PROTO;
	}
};


// This inline class implements a small iterator to traverse the base relocation table,
// may need expansion if this will be made available later for editing the image
class RelocationBlockView {
public:
	class RelocationBlockIterator {
		friend RelocationBlockView;
	public:
		RelocationBlockIterator& operator++() {
			TRACE_FUNCTION_PROTO;
			CurrentPosition = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
				reinterpret_cast<byte_t*>(CurrentPosition) +
				CurrentPosition->SizeOfBlock);
			return *this;
		}

		IMAGE_BASE_RELOCATION& operator*() {
			TRACE_FUNCTION_PROTO; return *CurrentPosition;
		}
		IMAGE_BASE_RELOCATION* operator->() {
			TRACE_FUNCTION_PROTO; return CurrentPosition;
		}

		bool operator!=(
			IN RelocationBlockIterator& Other
		) const {
			TRACE_FUNCTION_PROTO; return this->CurrentPosition != Other.CurrentPosition;
		};

	private:
		RelocationBlockIterator(
			IN byte_t* ImageBase,
			IN IMAGE_BASE_RELOCATION* BaseRelocationTable)
			: ImageBaseAddress(ImageBase),
			  CurrentPosition(BaseRelocationTable) {
			TRACE_FUNCTION_PROTO;
		}

		byte_t* const          ImageBaseAddress;
		IMAGE_BASE_RELOCATION* CurrentPosition;
	};

	RelocationBlockView(
		IN byte_t*               ImageBase,
		IN IMAGE_DATA_DIRECTORY& RelocationDirectory
	)
		: ImageBaseAddress(ImageBase),
		  BaseRelocationDirectory(RelocationDirectory) {
		TRACE_FUNCTION_PROTO;
	}

	RelocationBlockIterator begin() const {
		TRACE_FUNCTION_PROTO;
		return RelocationBlockIterator(ImageBaseAddress,
			reinterpret_cast<IMAGE_BASE_RELOCATION*>(
				BaseRelocationDirectory.VirtualAddress
				+ ImageBaseAddress));
	}
	RelocationBlockIterator end() const {
		TRACE_FUNCTION_PROTO;
		return RelocationBlockIterator(ImageBaseAddress,
			reinterpret_cast<IMAGE_BASE_RELOCATION*>(
				BaseRelocationDirectory.VirtualAddress
				+ ImageBaseAddress
				+ BaseRelocationDirectory.Size));
	}

private:
	byte_t* const         ImageBaseAddress;
	IMAGE_DATA_DIRECTORY& BaseRelocationDirectory;
};

// Implements the minimum required toolset to load, map and rebuild a portable executable.
// This module statically derives from and extends this base loader,
// in order to more easily work with coff images and parse or edit them.
export template<template<class> class... T>
class ImageLoader : 
	public T<ImageLoader<T...>>... {

	enum ImageHelpWorkState {
		IMAGEHELP_REFUSE = -2000, // A post detection found an issue with the object,
		                          // the object will refuse to operate as its invalid.

		IMAGEHELP_NEUTRAL = 0,          // No the object was just opened, no image currently mapped
		IMAGEHELP_MAPPED_AND_RELOCATED, // Specifies the object being mapped and relocated,
		                                // This allows to directly work with the image
										// instead of having to translate all pointers to and from rva's
		IMAGEHELP_RELOCATED             // The image was relocated but currently is not mapped
	};

public:
#pragma region ImageHelp setup and cleanup procedures
	enum FileAccessRights {
		FILE_READ = GENERIC_READ,
		FILE_WRITE = GENERIC_WRITE,
		FILE_READWRITE = GENERIC_READ | GENERIC_WRITE,
		FILE_READ_EXECUTE = GENERIC_READ | GENERIC_EXECUTE,
		FILE_READWRITE_EXECUTE = GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE
	};
	enum FileShareRights {
		FILESHARE_READ = FILE_SHARE_READ,
		FILESHARE_WRITE = FILE_SHARE_WRITE,
		FILESHARE_READWRITE = FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILESHARE_EXCLUSIVE = 0
	};

	ImageLoader(
		IN  std::string_view ImageFileName,
		OPT FileAccessRights FileAccess = FILE_READWRITE,
		OPT FileShareRights  FileSharing = FILESHARE_READ
	) {
		TRACE_FUNCTION_PROTO;

		// Try and open desired file, otherwise break out
		FileHandle = CreateFileA(ImageFileName.data(),
			static_cast<DWORD>(FileAccess),
			static_cast<DWORD>(FileSharing),
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (FileHandle == INVALID_HANDLE_VALUE)
			throw ImageHelpException(
				fmt::format("Failed to open file \"{}\" with {}",
					ImageFileName,
					GetLastError()),
				ImageHelpException::STATUS_FAILED_TO_OPEN_FILE);
		SPDLOG_INFO("Opened file \"{}\" with {} access rights",
			ImageFileName,
			FileAccess);
	}
	~ImageLoader() {
		TRACE_FUNCTION_PROTO;

		// Unmap and discard changes, then close the file and exit
		ReconstructOptionalAndUnmapImage(true);
		CloseHandle(FileHandle);
		SPDLOG_INFO("Closed file handle [{}]",
			FileHandle);
	}
#pragma endregion

#pragma region Primary mapping and reconstruction interfaces
	enum MapAccessRights {
		MAP_READ = PAGE_READONLY,
		MAP_READWRITE = PAGE_READWRITE,
		MAP_READ_EXECUTE = PAGE_EXECUTE_READ,
		MAP_READWRITE_EXECUTE = PAGE_EXECUTE_READWRITE
	};
	byte_t* MapImageIntoMemory(                           // Tries to map the loaded image into memory,
		                                                  // returns the actual location of where the image was mapped to.
		INOUT void*  DesiredMapping = nullptr,            // The desired location of where to map the image to in memory
		IN    size_t VirtualExtension = 0,                // The desired size of the file mapping object to be allocated,
		                                                  // must be bigger or equal to the images virtual size.
		IN    DWORD  Win32PageProtection = PAGE_READWRITE // A win32 compatible page protection to use for the mappings
	) {
		TRACE_FUNCTION_PROTO;
	
		// Validate image type by testing DOS header for magic value
		auto DosHeaderSignature = ReadFileTypeByOffset<
			decltype(IMAGE_DOS_HEADER::e_magic)>(
				OFFSET_OF(IMAGE_DOS_HEADER, e_magic));
		if (DosHeaderSignature != IMAGE_DOS_SIGNATURE)
			throw ImageHelpException(
				fmt::format("The image opened has a bad DOS header or is not a PE-COFF image, detected [{:#04x}]",
					DosHeaderSignature),
				ImageHelpException::STATUS_FAILED_SIGNATURE_TEST);
		SPDLOG_INFO("Validated image header, signature matches DOS header");

		// Dynamically resolve image size and other base meta data through minimal reads,
		// we also have to validate the PE header here before we even consider loading any of the file
		rva_t ModuleHeaderOffset = ReadFileTypeByOffset<rva_t>(
			OFFSET_OF(IMAGE_DOS_HEADER, e_lfanew));
		auto PeHeaderSignature = ReadFileTypeByOffset<
			decltype(IMAGE_NT_HEADERS::Signature)>(
				ModuleHeaderOffset + OFFSET_OF(IMAGE_NT_HEADERS, Signature));
		decltype(IMAGE_OPTIONAL_HEADER::Magic) OptHeaderSignature{};
		if (PeHeaderSignature == IMAGE_NT_SIGNATURE)
			OptHeaderSignature = ReadFileTypeByOffset<
			decltype(OptHeaderSignature)>(
				ModuleHeaderOffset + OFFSET_OF(IMAGE_NT_HEADERS, OptionalHeader.Magic));
		if (PeHeaderSignature != IMAGE_NT_SIGNATURE ||
			OptHeaderSignature != IMAGE_NT_OPTIONAL_HDR_MAGIC)
			throw ImageHelpException(
				fmt::format("The file opened has bad PE/Opt -headers, or is not a PE-COFF image, detected [{:08x}][{:04x}]",
					PeHeaderSignature,
					OptHeaderSignature),
				ImageHelpException::STATUS_FAILED_SIGNATURE_TEST);
		SPDLOG_INFO("Validated image header 2, signatures match PE headers");

		// We can now assume the file is half valid as the headers are signed, 
		// this means we can finally read its virtual size we need to reserve the required space for it,
		// additionally we check if the caller passed a desired extended size to the function.
		auto SizeOfImage = ReadFileTypeByOffset<
			decltype(IMAGE_OPTIONAL_HEADER::SizeOfImage)>(
				ModuleHeaderOffset + OFFSET_OF(IMAGE_NT_HEADERS,
					OptionalHeader.SizeOfImage));
		SPDLOG_INFO("Detected virtual image size of {}",
			SizeOfImage);
		if (VirtualExtension) {
			if (VirtualExtension < SizeOfImage)
				throw ImageHelpException(
					fmt::format("The passed virtual extension of {} was smaller than the detected virtual image size of {}",
						VirtualExtension,
						SizeOfImage),
					ImageHelpException::STATUS_SIZE_NOT_BIGGER_OR_EQUAL);
			SizeOfImage = static_cast<decltype(IMAGE_OPTIONAL_HEADER::SizeOfImage)>(VirtualExtension);
		} 


		// First we need to reserve a place of memory where we will be able to map the image,
		auto CheckOrThrowMemory = [](
			IN void* MemoryAddress
			) -> void {
				if (MemoryAddress == nullptr)
					throw ImageHelpException(
						"Memory assertion failed, presumable an allocation or commitment of memory failed to succeed",
						ImageHelpException::STATUS_INVALID_MEMORY_ADDRESS);
		};

		// Reserve 2gb of virtual memory of to where we map the image,
		// these are probably never all committed, they are just reserved,
		// to expand the image to its maximum physical/virtual size,
		// which is by spec 2gb as rva's are 31 bit to fit within a displacement32
		auto ReservedMemory2 = MakeSmartPointerWithVirtualAlloc<
			std::unique_ptr>(DesiredMapping,
				2097152ull * 1024 - 1,
				MEM_RESERVE,
				PAGE_NOACCESS);
		CheckOrThrowMemory(ReservedMemory2.get());
		SPDLOG_INFO("Reserved virtual memory at {} for image of size {}",
			static_cast<void*>(ReservedMemory2.get()),
			SizeOfImage);

		// Now we commit space for, and load the PE-COFF header including the data directory and section header table
		auto CombinedHeaderSizeFromImage = ReadFileTypeByOffset<
			decltype(IMAGE_OPTIONAL_HEADER::SizeOfHeaders)>(
				ModuleHeaderOffset + OFFSET_OF(IMAGE_NT_HEADERS, OptionalHeader.SizeOfHeaders));
		CheckOrThrowMemory(
			ReservedMemory2.CommitVirtualRange(
				ReservedMemory2.get(),
				CombinedHeaderSizeFromImage,
				Win32PageProtection));
		RequestReadWriteFileIo(FILE_IO_READ,
			0, 
			ReservedMemory2.get(),
			CombinedHeaderSizeFromImage);
		SPDLOG_INFO("Loaded PE-COFF image headers at reserved range");


		// Do basic image file tests to validate the image being suitable for processing or break out and abort
		auto ImageHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(
			ReservedMemory2.get() + ModuleHeaderOffset);
		auto ImageFileCheckPattern = 0;
		#define MERGE_CHECK_IMAGE(FailureExpression) (ImageFileCheckPattern |= (FailureExpression) << (--NumberOfMergeChecks))
		{
			// If any of these tests fail the image is not suitable for the processing that will/could be applied later on
			auto NumberOfMergeChecks = 5;
			MERGE_CHECK_IMAGE(ImageHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64);
			MERGE_CHECK_IMAGE(ImageHeader->FileHeader.SizeOfOptionalHeader > sizeof(IMAGE_OPTIONAL_HEADER));
			MERGE_CHECK_IMAGE(!(ImageHeader->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE));
			MERGE_CHECK_IMAGE(ImageHeader->OptionalHeader.SectionAlignment < PAGE_SIZE);
			MERGE_CHECK_IMAGE(std::popcount(ImageHeader->OptionalHeader.FileAlignment) != 1 ||
				ImageHeader->OptionalHeader.FileAlignment < 512 ||
				ImageHeader->OptionalHeader.FileAlignment > 512 * 128);
		}
		#undef MERGE_CHECK_IMAGE
		if (ImageFileCheckPattern)
			throw ImageHelpException(
				fmt::format("The image opened is not suitable for processing, detected [{:05b}] failures",
					ImageFileCheckPattern),
				ImageHelpException::STATUS_IMAGE_NOT_SUITABLE);	
		SPDLOG_INFO("Fully validated image, the image is suitable for processing");


		// we now commit and map all sections into their respective virtual addresses
		std::span SectionHeaderTable(IMAGE_FIRST_SECTION(
			ImageHeader),
			ImageHeader->FileHeader.NumberOfSections);
		for (IMAGE_SECTION_HEADER& Section : SectionHeaderTable) {

			// For each section the section will be mapped into its specified address range
			CheckOrThrowMemory(
				ReservedMemory2.CommitVirtualRange(
					ReservedMemory2.get() + Section.VirtualAddress,
					Section.Misc.VirtualSize,
					Win32PageProtection));
			RequestReadWriteFileIo(FILE_IO_READ,
				Section.PointerToRawData,
				ReservedMemory2.get() + Section.VirtualAddress,
				Section.SizeOfRawData);
			SPDLOG_INFO("Mapped section \"{}\" at address {} with size {}",
				std::string_view(reinterpret_cast<char*>(Section.Name), 
					IMAGE_SIZEOF_SHORT_NAME),
				static_cast<void*>(ReservedMemory2.get() + Section.VirtualAddress),
				Section.Misc.VirtualSize);
		}

		return (ImageMapping = std::move(ReservedMemory2)).get();
	}
	void ReconstructOptionalAndUnmapImage(   // Tries to rebuild the physical image file from the mapped image,
		                                     // by introspecting the headers and composition, image has to be loadable.
		bool DiscardChangesOnlyUnmap = false // Specifies that the made changes should be ignored 
		                                     // and the file shall not be rebuild from memory.
	) {
		TRACE_FUNCTION_PROTO; 

		// Check if any mapping is available
		if (ImageMapping) {
			
			// Check if we should reconstruct the image or discard its changes
			if (!DiscardChangesOnlyUnmap) {

				// Rebuild the image from virtual memory, this process is basically the mapping process in reverse
				auto SectionHeaderTable = GetCoffMemberByTemplateId<GET_SECTION_HEADERS>();
				for (auto& Section : SectionHeaderTable) {

					RequestReadWriteFileIo(FILE_IO_WRITE,
						Section.PointerToRawData,
						ImageMapping.get() + Section.VirtualAddress,
						Section.SizeOfRawData);
					SPDLOG_INFO("Reconstructed section \"{}\" at rva {:#x} with size {}",
						std::string_view(reinterpret_cast<char*>(Section.Name), 
							IMAGE_SIZEOF_SHORT_NAME),
						Section.VirtualAddress,
						Section.Misc.VirtualSize);
				}
			}

			// Unmap the image by releasing its virtual memory
			auto MappingAddressFallback = ImageMapping.get();
			ImageMapping.reset();
			SPDLOG_INFO("Unmapped image from {}",
				static_cast<void*>(MappingAddressFallback));
			return;
		}

		SPDLOG_DEBUG("No image was mapped at the time of the call to unmap, was this intended ?");
	}
#pragma endregion

	void RelocateImageToMappedOrOverrideBase( // Tries to relocate the image to its mapped image base or the base specified
		OPT uintptr_t DesiredImageBase = 0    // A image base the image should virtually be relocated to,
											  // the actual memory of the mapping isn't moved in the virtual address space
	) {
		TRACE_FUNCTION_PROTO;

		// Calculate delta between the target image base and the current virtual mapping
		if (!DesiredImageBase)
			DesiredImageBase = reinterpret_cast<uintptr_t>(ImageMapping.get());
		ptrdiff_t RelocationDelta = DesiredImageBase
			- GetCoffMemberByTemplateId<GET_OPTIONAL_HEADER>().ImageBase;
		if (RelocationDelta == 0) {

			// Special case, no need to apply relocations when there is nothing to relocate
			SPDLOG_DEBUG("Image doesnt need to be relocated, was this intended ?");
			return;
		}

		// Get metadata required for relocating (relocation directory and base relocation table)
		auto& RelocationDirectory = GetCoffMemberByTemplateId<
			GET_DATA_DIRECTORIES>()[IMAGE_DIRECTORY_ENTRY_BASERELOC];
		RelocationBlockView RelocationView(ImageMapping.get(),
			RelocationDirectory);

		// Walk base relocation block table (needs excessive debugging)
		for (auto& RelocationBlock : RelocationView) {

			struct IMAGE_RELOCATION_ENTRY {
				uint16_t Offset : 12;
				uint16_t Type : 4;
			};

			// Walk the per block based, base relocation view and apply
			std::span<IMAGE_RELOCATION_ENTRY> BaseRelcoationView(
				reinterpret_cast<IMAGE_RELOCATION_ENTRY*>(&RelocationBlock + 1),
				(RelocationBlock.SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) /
					sizeof(IMAGE_RELOCATION_ENTRY));
			for (auto& BaseRelocation : BaseRelcoationView) {

				// Calculate location of base relocation and treat relocation type
				void* BaseRelocationAddress = RelocationBlock.VirtualAddress
					+ BaseRelocation.Offset + ImageMapping.get();
				switch (BaseRelocation.Type) {
				case IMAGE_REL_BASED_ABSOLUTE:
					continue;
				case IMAGE_REL_BASED_DIR64:
					*reinterpret_cast<uintptr_t*>(BaseRelocationAddress) += RelocationDelta;
					SPDLOG_DEBUG("Applied base relocation at address {}",
						BaseRelocationAddress);
					break;

				default:
					throw ImageHelpException(
						fmt::format("The relocation entry at {} for {}, was unsupported",
							static_cast<void*>(&BaseRelocation),
							BaseRelocationAddress),
						ImageHelpException::STATUS_UNSUPPORTED_RELOCATION);
				}
			}

			SPDLOG_DEBUG("Apllied all relocations of relocation block at {}",
				static_cast<void*>(&RelocationBlock));
		}

		// Fixup headers and exit
		GetCoffMemberByTemplateId<GET_OPTIONAL_HEADER>().ImageBase = DesiredImageBase;
		SPDLOG_INFO("Fully relocated image and patched base, to virtual address {}",
			reinterpret_cast<void*>(DesiredImageBase));
	}

// Provides very raw Io interfaces to access the raw file on disk
#pragma region File Io helpers
	enum FileIoType {
		FILE_IO_READ,
		FILE_IO_WRITE
	};
	void RequestReadWriteFileIo(
		IN  FileIoType FileOperation,
		IN  offset_t   FileOffset,
		OUT byte_t*    RawDataBuffer,
		IN  size_t     RawDataLength
	) {
		TRACE_FUNCTION_PROTO;

		auto IoStatus = 0;
		if (FileOffset != FILE_OFFSET_DO_NOT_MOVE) {
			
			// Update internal file pointer location 
			IoStatus = SetFilePointerEx(FileHandle,
				LARGE_INTEGER{ .QuadPart = FileOffset },
				nullptr,
				FILE_BEGIN);
			if (IoStatus == 0)
				throw ImageHelpException(
					fmt::format("Failed to move file pointer of file [{}] to {:x} with {}",
						FileHandle,
						FileOffset,
						GetLastError()),
					ImageHelpException::STATUS_FAILED_FILE_POINTER);
			SPDLOG_DEBUG("Moved file pointer of file [{}] to offset {:x}",
				FileHandle,
				FileOffset);
		}

		// Execute Io operation and store status
		DWORD BytesProcessedInIoRequest = 0;
		switch (FileOperation) {
		case FILE_IO_READ:
			IoStatus = ReadFile(FileHandle,
				RawDataBuffer,
				static_cast<DWORD>(RawDataLength),
				&BytesProcessedInIoRequest,
				nullptr);
			break;
		case FILE_IO_WRITE:
			IoStatus = WriteFile(FileHandle,
				RawDataBuffer,
				static_cast<DWORD>(RawDataLength),
				&BytesProcessedInIoRequest,
				nullptr);
		}

		// Check Io operation for error and throw
		if (IoStatus == 0)
			throw ImageHelpException(
				fmt::format("Failed to read/write on file [{}] of size {:x} with {}",
					FileHandle,
					RawDataLength,
					GetLastError()),
				ImageHelpException::STATUS_FAILED_IO_OPERATION);
		SPDLOG_INFO("Read/Wrote file [{}] at [{:x}:{:x}]",
			FileHandle,
			FileOffset, RawDataLength);
	}
	enum : offset_t {
		FILE_OFFSET_DO_NOT_MOVE = -1
	};
	template<typename T>           // 
		requires(std::is_pod_v<T>) // Type to be read must be pod type data, aka raw
	T ReadFileTypeByOffset(        // Reads pod type data at as specified offset as type T
		IN offset_t FileOffset     // The offset to read the pod type data from,
		                           // if FILE_OFFSET_DO_NOT_MOVE is specified the read continues from the current location
	) {
		TRACE_FUNCTION_PROTO;

		T ReturnType;
		RequestReadWriteFileIo(FILE_IO_READ,
			FileOffset,
			reinterpret_cast<byte_t*>(&ReturnType),
			sizeof(T));
		return ReturnType;
	}
#pragma endregion

#pragma region File region helpers
	enum SupportedCoffGetters {
		GET_DOS_HEADER,
		GET_IMAGE_HEADER,
		GET_FILE_HEADER,
		GET_OPTIONAL_HEADER,
		GET_DATA_DIRECTORIES,
		GET_SECTION_HEADERS,
	};

// Temporary private scope in order to hide translation layer from public
private:
	template<SupportedCoffGetters TypeTag>
	struct TypeEnum;
	#define MAP_ENUM_TO_TYPE(Enum, Type) template<>\
	struct TypeEnum<Enum> {\
		using type = Type;\
	};
	MAP_ENUM_TO_TYPE(GET_DOS_HEADER, IMAGE_DOS_HEADER&);
	MAP_ENUM_TO_TYPE(GET_IMAGE_HEADER, IMAGE_NT_HEADERS&);
	MAP_ENUM_TO_TYPE(GET_FILE_HEADER, IMAGE_FILE_HEADER&);
	MAP_ENUM_TO_TYPE(GET_OPTIONAL_HEADER, IMAGE_OPTIONAL_HEADER&);
	MAP_ENUM_TO_TYPE(GET_DATA_DIRECTORIES, std::span<IMAGE_DATA_DIRECTORY>);
	MAP_ENUM_TO_TYPE(GET_SECTION_HEADERS, std::span<IMAGE_SECTION_HEADER>);
	#undef MAP_ENUM_TO_TYPE
public:

	template<SupportedCoffGetters TypeTag>
	TypeEnum<TypeTag>::type GetCoffMemberByTemplateId() const {
		TRACE_FUNCTION_PROTO;
		using type = TypeEnum<TypeTag>::type;

		if constexpr (TypeTag == GET_DOS_HEADER) {
			return *reinterpret_cast<std::decay_t<type>*>(ImageMapping.get());
		}
		else if constexpr (TypeTag == GET_IMAGE_HEADER) {
			return *reinterpret_cast<std::decay_t<type>*>(ImageMapping.get() +
				GetCoffMemberByTemplateId<GET_DOS_HEADER>().e_lfanew);
		}
		else if constexpr (TypeTag == GET_FILE_HEADER) {
			return GetCoffMemberByTemplateId<GET_IMAGE_HEADER>().FileHeader;
		}
		else if constexpr (TypeTag == GET_OPTIONAL_HEADER) {
			return GetCoffMemberByTemplateId<GET_IMAGE_HEADER>().OptionalHeader;
		}
		else if constexpr (TypeTag == GET_DATA_DIRECTORIES) {
			auto& OptionalHeader = GetCoffMemberByTemplateId<GET_OPTIONAL_HEADER>();
			return std::span(OptionalHeader.DataDirectory,
				OptionalHeader.NumberOfRvaAndSizes);
		}
		else if constexpr (TypeTag == GET_SECTION_HEADERS) {
			return std::span(IMAGE_FIRST_SECTION(
				&GetCoffMemberByTemplateId<GET_IMAGE_HEADER>()),
				GetCoffMemberByTemplateId<GET_FILE_HEADER>().NumberOfSections);
		}
	}
#pragma endregion

	byte_t* GetImageFileMapping() const {
		TRACE_FUNCTION_PROTO; return ImageMapping.get();
	}

// Minimal private sector, just required to store the file handled and a object to reference the mapping
private:
	VirtualPointer<std::unique_ptr> 
	       ImageMapping;
	HANDLE FileHandle = INVALID_HANDLE_VALUE;
};


// A skillset for the ImageLoader class template, this can be installed into the image loaders statically
// this provides access to higher level data structures of the PE-COFF file such as functions, sections
// and more in the future, as this class will be used as the image editor to allow the virtualizer
// to write to the image in an abstracted way.
template<typename T>
class ImageEditor 
	: public CrtpHelp<T> {
public:
#pragma region Exception directory parser and editor components
	std::span<const RUNTIME_FUNCTION> GetRuntimeFunctionTable() const {
		TRACE_FUNCTION_PROTO;

		// Locate the runtime function table and test, then return view
		auto& Underlying = this->GetUnderlyingCrtpBase();
		IMAGE_DATA_DIRECTORY& ExceptionDirectroy = Underlying.GetCoffMemberByTemplateId<
			T::GET_DATA_DIRECTORIES>()[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		
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
};

// Special public name for the image loader and image editor (etc.) tool
export using ImageHelp = ImageLoader<ImageEditor>;
