#include "pch.h"
#include "portable_executable.h"
#include "peutils.h"

PortableExecutable pe::Open( const std::vector<uint8_t>& pe_data ) {
  return PortableExecutable( pe_data );
}

PortableExecutable pe::Build( const std::vector<uint8_t>& header,
                              const std::vector<Section>& sections ) {
  std::vector<uint8_t> pe_data;

  uint32_t section_data_size = 0;

  for ( const auto& section : sections )
    section_data_size += section.data_.size();

  // Reserve all required memory to avoid memory performance footprint
  pe_data.reserve( header.size() + section_data_size );

  // Append the header
  pe_data.insert( pe_data.begin(), header.begin(), header.end() );

  const auto nt_headers = *peutils::GetNtHeaders( &pe_data[ 0 ] );

  auto sections_copy = sections;

  for ( size_t i = 0; i < sections_copy.size(); ++i ) {
    auto& section = sections_copy[ i ];

    // If there is no initial data specified by the section headers
    if ( !section.section_header_.PointerToRawData ) {
      // Check if the user has added data manually on the section
      if ( section.data_.size() > 0 ) {
        // Ensure that we do not go out of bounds
        assert( ( i - 1 ) > 0 );

        const auto& previous_section = sections_copy[ i - 1 ];

        const auto raw_data_offset = peutils::AlignUp(
            pe_data.size(), nt_headers.OptionalHeader.FileAlignment );

        // Insert padding data before the real code
        pe_data.insert( pe_data.end(), raw_data_offset - pe_data.size(), 0xCC );

        // They must be equal to each other, if not, something very bad occured
        assert( pe_data.size() == raw_data_offset );

        // Set the PointerToRawData appropriately
        section.section_header_.PointerToRawData = raw_data_offset;

        section.section_header_.VirtualAddress = peutils::AlignUp(
            previous_section.section_header_.VirtualAddress +
                previous_section.section_header_.Misc.VirtualSize,
            nt_headers.OptionalHeader.SectionAlignment );
      } else {
        // If no section data added, do not insert any data into the pe_data
        continue;
      }
    }

    // Get an updated version of nt header, because when we insert data into
    // pe_data above, the buffer gets reallocated at another address
    const auto nt_headers2 = peutils::GetNtHeaders( pe_data.data() );

    // Update the section header in the pe header
    const auto current_section_header = IMAGE_FIRST_SECTION( nt_headers2 ) + i;
    memcpy( current_section_header, &section.section_header_,
            sizeof( section.section_header_ ) );

    assert( pe_data.size() == section.section_header_.PointerToRawData );
    pe_data.insert( pe_data.end(), section.data_.begin(), section.data_.end() );

    // Insert padding data at the end to fill the entire section due to
    // alignment
    pe_data.insert(
        pe_data.end(),
        section.section_header_.SizeOfRawData - section.data_.size(), 0xCC );
  }

  const auto new_pe = PortableExecutable( pe_data );

  auto& last_section = sections_copy[ sections_copy.size() - 1 ];

  // Adjusts the nt header for the sections
  new_pe.nt_headers_->FileHeader.NumberOfSections =
      static_cast<WORD>( sections_copy.size() );

  new_pe.nt_headers_->OptionalHeader.SizeOfImage =
      last_section.section_header_.VirtualAddress +
      last_section.section_header_.Misc.VirtualSize;

  return new_pe;
}

PortableExecutable::PortableExecutable( const std::vector<uint8_t>& pe_data )
    : nt_headers_( nullptr ), dos_headers_( nullptr ) {
  pe_data_ = pe_data;

  const auto pe_data_ptr = &pe_data_[ 0 ];

  dos_headers_ = reinterpret_cast<IMAGE_DOS_HEADER*>( pe_data_ptr );

  nt_headers_ = peutils::GetNtHeaders( pe_data_ptr );
}

PortableExecutable::PortableExecutable( const PortableExecutable& pe2 )
    : dos_headers_( nullptr ), nt_headers_( nullptr ) {
  pe_data_ = pe2.pe_data_;

  const auto pe_data_ptr = &pe_data_[ 0 ];

  dos_headers_ = reinterpret_cast<IMAGE_DOS_HEADER*>( pe_data_ptr );

  nt_headers_ = peutils::GetNtHeaders( pe_data_ptr );
}

bool PortableExecutable::IsValidPortableExecutable() const {
  if ( !dos_headers_ || !nt_headers_ )
    return false;

  if ( dos_headers_->e_magic != IMAGE_DOS_SIGNATURE )
    return false;

  if ( nt_headers_->Signature != IMAGE_NT_SIGNATURE )
    return false;

  return true;
}

std::vector<Section> PortableExecutable::CopySections() const {
  const auto first_section = IMAGE_FIRST_SECTION( nt_headers_ );
  const auto section_count = nt_headers_->FileHeader.NumberOfSections;

  std::vector<Section> sections;

  sections.reserve( section_count );

  for ( int i = 0; i < section_count; ++i ) {
    const auto section_header = &first_section[ i ];
    sections.push_back( CopySection( section_header ) );
  }

  return sections;
}

Section PortableExecutable::CopySection(
    const IMAGE_SECTION_HEADER* section_header ) const {
  return Section( *section_header, pe_data_ );
}

SectionHeaders PortableExecutable::GetSectionHeaders() const {
  const auto first_section = IMAGE_FIRST_SECTION( nt_headers_ );
  const auto section_count = nt_headers_->FileHeader.NumberOfSections;

  std::vector<IMAGE_SECTION_HEADER*> section_headers;

  section_headers.reserve( section_count );

  for ( int i = 0; i < section_count; ++i ) {
    section_headers.push_back( &first_section[ i ] );
  }

  // Wrap the section header inside a class that has helper functions to
  // convenience =)
  SectionHeaders section_headers_obj;
  section_headers_obj.headers = section_headers;

  return section_headers_obj;
}

std::vector<Import> PortableExecutable::GetImports() const {
  std::vector<Import> imports;

  const auto pe_data_ptr = const_cast<uint8_t*>( &pe_data_[ 0 ] );

  const auto& import_directory =
      nt_headers_->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ];

  // is there an import directory?
  if ( !import_directory.VirtualAddress )
    return {};

  auto sections = GetSectionHeaders();

  // For each import desc
  for ( auto import_descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            pe_data_ptr +
            sections.RvaToFileOffset( import_directory.VirtualAddress ) );
        import_descriptor->Name; ++import_descriptor ) {
    // Read the function associated dll name
    const auto associated_dll_name = reinterpret_cast<const char*>(
        pe_data_ptr + sections.RvaToFileOffset( import_descriptor->Name ) );

    // Since we are only looking for a function from a specific DLL, check if
    // the DLL name is the one we want that contains the function we want to
    // hook to avoid going though every import desc.
    auto original_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
        pe_data_ptr +
        sections.RvaToFileOffset( import_descriptor->OriginalFirstThunk ) );

    uint32_t function_address = import_descriptor->FirstThunk;

    // For each import thunk
    for ( ; original_thunk->u1.AddressOfData;
          ++original_thunk,
          function_address += sizeof( import_descriptor->FirstThunk ) ) {
      assert( original_thunk );

      if ( IMAGE_SNAP_BY_ORDINAL( original_thunk->u1.Ordinal ) ) {
        assert( false && "Handle ordinal" );
      } else {
        // Read the import info in that thunk
        const auto import_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
            pe_data_ptr +
            sections.RvaToFileOffset( original_thunk->u1.AddressOfData ) );

        Import import;
        import.associated_module = associated_dll_name;
        import.function_addr_rva = function_address;
        import.function_name = import_name->Name;

        imports.push_back( import );
      }
    }
  }

  return imports;
}

std::vector<Export> PortableExecutable::GetExports() const {
  std::vector<Export> exports;

  const auto& export_directory =
      nt_headers_->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];

  // Is there an export directory?
  if ( !export_directory.VirtualAddress )
    return {};

  auto sections = GetSectionHeaders();

  const auto pe_data_ptr = const_cast<uint8_t*>( &pe_data_[ 0 ] );

  const auto exports_dir = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(
      pe_data_ptr +
      sections.RvaToFileOffset( export_directory.VirtualAddress ) );

  const auto names = reinterpret_cast<uint32_t*>(
      pe_data_ptr + sections.RvaToFileOffset( exports_dir->AddressOfNames ) );
  const auto ordinals = reinterpret_cast<uint16_t*>(
      pe_data_ptr +
      sections.RvaToFileOffset( exports_dir->AddressOfNameOrdinals ) );
  const auto addresses = reinterpret_cast<uint32_t*>(
      pe_data_ptr +
      sections.RvaToFileOffset( exports_dir->AddressOfFunctions ) );

  for ( uint32_t i = 0; i < exports_dir->NumberOfNames; ++i ) {
    const auto function_name = reinterpret_cast<const char*>(
        pe_data_ptr + sections.RvaToFileOffset( names[ i ] ) );

    const uint32_t function_addr = addresses[ ordinals[ i ] ];

    Export exprt;
    exprt.function_name = function_name;
    exprt.function_addr_rva = function_addr;

    exports.push_back( exprt );
  }

  return exports;
}

void PortableExecutable::DisableASLR() {
  if ( ( nt_headers_->OptionalHeader.DllCharacteristics &
         IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE ) ==
       IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE )
    nt_headers_->OptionalHeader.DllCharacteristics &=
        ~IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
}

void PortableExecutable::Relocate( const uintptr_t new_image_base_address ) {
  auto sections = GetSectionHeaders();

  const auto image_ptr = const_cast<uint8_t*>( GetPeImagePtr() );

  EachRelocation( [&]( IMAGE_BASE_RELOCATION* reloc_block, uintptr_t rva,
                       Relocation* reloc ) {
    if ( reloc->type == IMAGE_REL_BASED_HIGHLOW ||
         reloc->type == IMAGE_REL_BASED_DIR64 ) {
      const auto file_offset = sections.RvaToFileOffset( rva );
      *( uint32_t* )( image_ptr + file_offset ) += new_image_base_address;
    }
  } );
}

std::vector<uint8_t> PortableExecutable::CopyHeaderData() const {
  return std::vector<uint8_t>(
      pe_data_.begin(),
      pe_data_.begin() + nt_headers_->OptionalHeader.SizeOfHeaders );
}

uint8_t* PortableExecutable::GetPeImagePtr() {
  return &pe_data_[ 0 ];
}

const uint8_t* PortableExecutable::GetPeImagePtr() const {
  return &pe_data_[ 0 ];
}

const std::vector<uint8_t>& PortableExecutable::GetPeData() const {
  return pe_data_;
}

std::vector<uint8_t>& PortableExecutable::GetPeData() {
  return pe_data_;
}

IMAGE_NT_HEADERS* PortableExecutable::GetNtHeaders() const {
  return nt_headers_;
}