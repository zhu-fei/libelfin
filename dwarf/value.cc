#include "internal.hh"

#include <cstring>

using namespace std;

DWARFPP_BEGIN_NAMESPACE

sec_offset
value::get_section_offset() const
{
        return cu->info.offset + offset;
}

taddr
value::as_address() const
{
        if (form != DW_FORM::addr)
                throw value_type_mismatch("cannot read " + to_string(typ) + " as address");

        cursor cur(cu->subsec, offset);
        return cur.address();
}

const void *
value::as_block(size_t *size_out) const
{
        // XXX It appears that expressions are often encoded as
        // blocks, not as exprlocs.  Need DW_AT-aware types?
        // XXX Blocks can contain all sorts of things, including
        // references, which couldn't be resolved by callers in the
        // current minimal API.
        // XXX Does automatic coercion
        cursor cur(cu->subsec, offset);
        switch (form) {
        case DW_FORM::block1:
                *size_out = cur.fixed<uint8_t>();
                break;
        case DW_FORM::block2:
                *size_out = cur.fixed<uint16_t>();
                break;
        case DW_FORM::block4:
                *size_out = cur.fixed<uint32_t>();
                break;
        case DW_FORM::block:
        case DW_FORM::exprloc:
                *size_out = cur.uleb128();
                break;
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as block");
        }
        cur.ensure(*size_out);
        return cur.pos;
}

uint64_t
value::as_uconstant() const
{
        // XXX Does automatic coercion
        cursor cur(cu->subsec, offset);
        switch (form) {
        case DW_FORM::data1:
                return cur.fixed<uint8_t>();
        case DW_FORM::data2:
                return cur.fixed<uint16_t>();
        case DW_FORM::data4:
                return cur.fixed<uint32_t>();
        case DW_FORM::data8:
                return cur.fixed<uint64_t>();
        case DW_FORM::udata:
                return cur.uleb128();
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as uconstant");
        }
}

int64_t
value::as_sconstant() const
{
        // XXX Does automatic coercion
        cursor cur(cu->subsec, offset);
        switch (form) {
        case DW_FORM::data1:
                return cur.fixed<int8_t>();
        case DW_FORM::data2:
                return cur.fixed<int16_t>();
        case DW_FORM::data4:
                return cur.fixed<int32_t>();
        case DW_FORM::data8:
                return cur.fixed<int64_t>();
        case DW_FORM::sdata:
                return cur.sleb128();
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as sconstant");
        }
}

expr
value::as_exprloc() const
{
        // XXX Does automatic coercion
        cursor cur(cu->subsec, offset);
        size_t size;
        // Prior to DWARF 4, exprlocs were encoded as blocks.
        switch (form) {
        case DW_FORM::exprloc:
        case DW_FORM::block:
                size = cur.uleb128();
                break;
        case DW_FORM::block1:
                size = cur.fixed<uint8_t>();
                break;
        case DW_FORM::block2:
                size = cur.fixed<uint16_t>();
                break;
        case DW_FORM::block4:
                size = cur.fixed<uint32_t>();
                break;
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as exprloc");
        }
        return expr(cu, cur.section_offset(), size);
}

bool
value::as_flag() const
{
        switch (form) {
        case DW_FORM::flag: {
                cursor cur(cu->subsec, offset);
                return cur.fixed<ubyte>() != 0;
        }
        case DW_FORM::flag_present:
                return true;
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as flag");
        }
}

die
value::as_reference() const
{
        sec_offset off;
        // XXX Would be nice if we could avoid this.  The cursor is
        // all overhead here.
        cursor cur(cu->subsec, offset);
        switch (form) {
        case DW_FORM::ref1:
                off = cur.fixed<ubyte>();
                break;
        case DW_FORM::ref2:
                off = cur.fixed<uhalf>();
                break;
        case DW_FORM::ref4:
                off = cur.fixed<uword>();
                break;
        case DW_FORM::ref8:
                off = cur.fixed<uint64_t>();
                break;
        case DW_FORM::ref_udata:
                off = cur.uleb128();
                break;

                // XXX Implement
        case DW_FORM::ref_addr:
                throw runtime_error("DW_FORM::ref_addr not implemented");
        case DW_FORM::ref_sig8:
                throw runtime_error("DW_FORM::ref_sig8 not implemented");

        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as reference");
        }

        die d(cu);
        d.read(off);
        return d;
}

void
value::as_string(string &buf) const
{
        size_t size;
        const char *p = as_cstr(&size);
        buf.resize(size);
        memmove(&buf.front(), p, size);
}

string
value::as_string() const
{
        size_t size;
        const char *s = as_cstr(&size);
        return string(s, size);
}

const char *
value::as_cstr(size_t *size_out) const
{
        cursor cur(cu->subsec, offset);
        switch (form) {
        case DW_FORM::string:
                return cur.cstr(size_out);
        case DW_FORM::strp: {
                sec_offset off = cur.offset();
                cursor scur(cu->file->get_sec_str(), off);
                return scur.cstr(size_out);
        }
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as string");
        }
}

sec_offset
value::as_sec_offset() const
{
        // Prior to DWARF 4, sec_offsets were encoded as data4 or
        // data8.
        cursor cur(cu->subsec, offset);
        switch (form) {
        case DW_FORM::data4:
                return cur.fixed<uint32_t>();
        case DW_FORM::data8:
                return cur.fixed<uint64_t>();
        case DW_FORM::sec_offset:
                return cur.offset();
        default:
                throw value_type_mismatch("cannot read " + to_string(typ) + " as sec_offset");
        }
}

void
value::resolve_indirect(DW_AT name)
{
        if (form != DW_FORM::indirect)
                return;

        cursor c(cu->subsec, offset);
        DW_FORM form;
        do {
                form = (DW_FORM)c.uleb128();
        } while (form == DW_FORM::indirect);
        typ = attribute_spec(name, form).type;
        offset = c.section_offset();
}

string
to_string(const value &v)
{
        switch (v.get_type()) {
        case value::type::invalid:
                return "<invalid value type>";
        case value::type::address:
                return "0x" + to_hex(v.as_address());
        case value::type::block: {
                size_t size;
                const char *b = (const char*)v.as_block(&size);
                string res = ::to_string(size) + " byte block:";
                for (size_t pos = 0; pos < size; ++pos) {
                        res += ' ';
                        res += to_hex(b[pos]);
                }
                return res;
        }
        case value::type::constant:
                return "0x" + to_hex(v.as_uconstant());
        case value::type::uconstant:
                return ::to_string(v.as_uconstant());
        case value::type::sconstant:
                return ::to_string(v.as_sconstant());
        case value::type::exprloc:
                // XXX
                return "<exprloc>";
        case value::type::flag:
                return v.as_flag() ? "true" : "false";
        case value::type::lineptr:
                return "<lineptr 0x" + to_hex(v.as_sec_offset()) + ">";
        case value::type::loclistptr:
                return "<loclistptr 0x" + to_hex(v.as_sec_offset()) + ">";
        case value::type::macptr:
                return "<macptr 0x" + to_hex(v.as_sec_offset()) + ">";
        case value::type::rangelistptr:
                return "<rangelistptr 0x" + to_hex(v.as_sec_offset()) + ">";
        case value::type::reference:
                return "<0x" + to_hex(v.as_reference().get_section_offset()) + ">";
        case value::type::string:
                return v.as_string();
        }
        return "<unexpected value type " + to_string(v.get_type()) + ">";
}

DWARFPP_END_NAMESPACE
