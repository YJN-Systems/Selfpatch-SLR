#include "layout_hash.h"

#include <cstdint>
#include <cstring>
#include <string>

#include <safe-md5.h>

namespace
{

void append_u64(std::string &buf, std::uint64_t v)
{
	for (unsigned i = 0; i < 8; i++)
		buf.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
}

void append_size(std::string &buf, std::size_t v)
{
	append_u64(buf, static_cast<std::uint64_t>(v));
}

void append_string(std::string &buf, const std::string &s)
{
	append_size(buf, s.size());
	buf.append(s);
}

} // namespace

std::array<std::byte, 16> layout_hash(const TargetType &target)
{
	std::string buf;

	append_string(buf, "spslr-layout-hash-v1");

	append_string(buf, target.name());
	append_size(buf, target.size());

	for (const auto &[off, field] : target.fields()) {
		append_string(buf, field.name);
		append_size(buf, field.size);
		append_size(buf, field.offset);
		append_size(buf, field.alignment);
		append_size(buf, field.flags);
	}

	unsigned char digest[16];
	md5_buffer(buf.data(), buf.size(), digest);

	std::array<std::byte, 16> out;
	for (std::size_t i = 0; i < out.size(); i++)
		out[i] = static_cast<std::byte>(digest[i]);

	return out;
}
