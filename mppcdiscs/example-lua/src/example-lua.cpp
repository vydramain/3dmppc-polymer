#include <cstdint>

#include "pdk/de/rv_de.hpp"
#include "pdk/ve/rv_ve.hpp"

namespace example_lua
{

namespace
{

struct rv_example_lua_texheader {
	uint16_t version;
};

} // namespace

class rv_dmain : public rv_pdk::rv_de
{
public:
	int64_t disc_initialize(rv_pdk::rv_pdko &pdk) override;
	void frame_update(float dt) override;
	void frame_render() override;
	bool disc_release() const override
	{
		return release_;
	}
	void disc_shutdown() override;
	const char *disc_tilte() const
	{
		return "example-lua";
	};

private:
	rv_pdk::rv_pdko *pdk_ = nullptr;
	bool release_ = false;
};

} // namespace example_lua
