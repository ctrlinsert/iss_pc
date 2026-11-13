// SPDX-License-Identifier: GPL-2.0-or-later
#include <utility>
#include "../src/iss_native_resolution.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <algorithm>
#include <string>
static uint32_t read32(const unsigned char* p){uint32_t x;memcpy(&x,p,4);return x;}
static uint64_t read64(const unsigned char* p){uint64_t x;memcpy(&x,p,8);return x;}
static void fixture(std::vector<unsigned char>& image){
    image.assign(0xb00000,0xcc);
    for(const auto& s:iss_native_sites)memcpy(image.data()+s.offset,s.before,s.length);
    memcpy(image.data()+0x58030c,iss_native_projection,sizeof(iss_native_projection));
    memcpy(image.data()+0x5802e6,iss_native_aspect_width,sizeof(iss_native_aspect_width));
    memcpy(image.data()+0x171310,iss_exposure_allocator,sizeof(iss_exposure_allocator));
    for(const auto& s:iss_exposure_sites)memcpy(image.data()+s.offset,s.before,5);
    for(const auto& s:iss_map_rt_sites)memcpy(image.data()+s.offset,s.before,s.length);
    memcpy(image.data()+ISS_MAP_RT_FLOAT_LOAD,iss_map_rt_float_load,sizeof(iss_map_rt_float_load));
}
// tests/native_resolution_cases.txt is shared with test_native_resolution_launcher.py (the launchers'
// copy of this arithmetic): both must give the same answers.
static unsigned shared_cases(){
    std::string path=__FILE__;path=path.substr(0,path.find_last_of("/\\")+1)+"native_resolution_cases.txt";
    std::ifstream in(path);assert(in);unsigned count=0;std::string line;
    while(std::getline(in,line)){
        if(line.empty()||line[0]=='#')continue;
        char kind[16];int a=0,b=0,c=0,d=0,e=0,f=0,g=0;
        if(sscanf(line.c_str(),"plan %d %d -> %d %d",&a,&b,&c,&d)==4){
            IssNativePlan p;assert(iss_native_plan({a,b,1,0},&p)&&int(p.width)==c&&int(p.height)==d);
        }else if(sscanf(line.c_str(),"plan %d %d -> %15s",&a,&b,kind)==3){
            IssNativePlan p;assert(!strcmp(kind,"refused")&&!iss_native_plan({a,b,1,0},&p));
        }else if(sscanf(line.c_str(),"pick %d %d -> %d %d %d %d %d",&a,&b,&c,&d,&e,&f,&g)==7){
            IssNativePick r;assert(iss_native_pick(a,uint32_t(b),&r)==c);
            assert(r.percent==d&&int(r.width)==e&&int(r.height)==f&&r.error==g);
        }else if(sscanf(line.c_str(),"step %d %d -> %d",&a,&b,&c)==3){
            assert(iss_native_step_for_percent(a,b)==c);
        }else if(sscanf(line.c_str(),"retarget %d %d %d -> %d",&a,&b,&c,&d)==4){
            assert(iss_native_retarget(a,b,c)==d);
        }else assert(!"unknown line in native_resolution_cases.txt");
        count++;
    }
    return count;
}
int main(int argc,char** argv){
    assert(shared_cases()>=60);
    IssNativeConfig fresh;
    assert(iss_native_config(nullptr,&fresh));
    assert(fresh.output==0 && fresh.percent==100 && fresh.ui_16_9==1 && fresh.hud_edges==0);
    FILE* legacy=tmpfile();assert(legacy);fputs("iss_native_ui_16_9=0\niss_native_hud_edges=1\n",legacy);rewind(legacy);
    assert(iss_native_config(legacy,&fresh)&&fresh.ui_16_9==0&&fresh.hud_edges==1);fclose(legacy);
    IssNativePlan p;
    iss_native_set_custom(5120,2160);   // 5K2K: the custom slot runs the same checks as the presets
    for(int output=1;output<ISS_NATIVE_PRESETS;output++){
        uint32_t ow,oh;assert(iss_native_output_size(output,&ow,&oh));
        assert(iss_native_plan({output,100,1},&p));
        assert(p.width==ow && p.height==oh && p.output_width==ow);
        assert(p.ui_height==1080);
        const double aspect=double(p.output_width)/p.output_height;
        assert(std::abs(double(p.ui_width)/p.ui_height-aspect)<0.001);
        // Hor+ geometry: a fixed vertical half-FOV grows horizontally with aspect.
        double vfov=60.0*3.141592653589793/180;
        double hfov=2*std::atan(std::tan(vfov/2)*aspect);
        assert(std::abs(2*std::atan(std::tan(hfov/2)/aspect)-vfov)<1e-12);
        const uint64_t needed=uint64_t((p.width+15)/16)*((p.height+15)/16)*128;
        assert(p.light_entries>=needed && p.light_entries>=0x320000 && p.light_entries%65536==0);
        for(int scale=25;scale<=200;scale++){
            bool ok=iss_native_plan({output,scale,1},&p);
            if(ok){assert(!(p.width%2) && !(p.height%2));assert(uint64_t(p.width)*p.height<=ISS_NATIVE_MAX_PIXELS);}
            else assert(!p.width && !p.height);
        }
    }
    assert(iss_native_plan({3,100,1},&p) && p.width==3440 && p.height==1440 && p.ui_width==2580 && p.light_entries==0x320000);
    assert(iss_native_plan({5,100,1},&p) && p.width==5120 && p.height==1440 && p.ui_width==3840 && p.light_entries==3735552);
    assert(iss_native_plan({4,100,1},&p) && p.light_entries==4194304);
    assert(iss_native_plan({3,50,1},&p) && p.width==1720 && p.height==720);
    // 8K budget: 7680x2160 at 100%, 4K at 200% (exactly 8K); 7680x2160 at 150% is over it.
    assert(iss_native_plan({6,100,1},&p) && p.width==7680 && p.height==2160 && p.ui_width==3840);
    assert(iss_native_plan({4,200,1},&p) && p.width==7680 && p.height==4320);
    assert(iss_native_plan({7,100,1},&p) && p.width==5120 && p.height==2160 && p.ui_width==2560);
    iss_native_set_custom(7680,2160);assert(iss_native_plan({7,100,1},&p) && p.width==7680);
    for(auto wh:{std::pair<unsigned,unsigned>{1920,1200},{5121,2160},{1024,576},{15362,4320},{0,2160},{3840,4322}}){
        iss_native_set_custom(wh.first,wh.second);assert(!iss_native_plan({7,100,1},&p));   // 16:10, odd, small, large
    }
    for(auto c:{IssNativeConfig{0,100,1},{7,100,1},{8,100,1},{3,0,1},{3,201,1},{3,100,2},{6,150,1}})assert(!iss_native_plan(c,&p));
    FILE* custom=tmpfile();assert(custom);fputs("iss_native_output=7\niss_native_width=3840\niss_native_height=1600\n",custom);rewind(custom);
    {IssNativeConfig cc;assert(iss_native_config(custom,&cc) && cc.output==7);fclose(custom);
     assert(iss_native_plan(cc,&p) && p.width==3840 && p.height==1600);}
    custom=tmpfile();assert(custom);fputs("iss_native_width=20000\n",custom);rewind(custom);
    {IssNativeConfig cc;assert(!iss_native_config(custom,&cc));fclose(custom);}
    iss_native_set_custom(0,0);
    FILE* f=tmpfile();assert(f);fputs("iss_native_output=5\niss_native_percent=100\niss_native_hud_edges=0\n",f);rewind(f);
    IssNativeConfig c;assert(iss_native_config(f,&c) && c.output==5 && c.percent==100 && c.hud_edges==0);fclose(f);
    f=tmpfile();assert(f);fputs("iss_native_output=3\niss_native_percent=100oops\n",f);rewind(f);assert(!iss_native_config(f,&c));fclose(f);
    // Temporal modes: 1080p output at 1.5x, 1.7x, 2x and 3x is 720p, 636p, 540p and 360p of scene
    f=tmpfile();assert(f);fputs("iss_native_output=1\niss_temporal_mode=3\n",f);rewind(f);
    assert(iss_native_config(f,&c) && c.temporal_mode==3 && c.percent==100);fclose(f);
    assert(iss_native_apply_temporal_mode(&c,3) && c.percent==50);
    assert(iss_native_plan(c,&p) && p.width==960 && p.height==540);
    const int mode_scene[4][3]={{1,67,724},{2,59,638},{3,50,540},{4,33,356}};
    for(auto& m:mode_scene){c={1,100,1};assert(iss_native_apply_temporal_mode(&c,m[0]) && c.percent==m[1]);
        assert(iss_native_plan(c,&p) && int(p.height)==m[2]);}
    c={0,100,1};assert(!iss_native_apply_temporal_mode(&c,2) && c.percent==100);   // no native output
    c={1,80,1};assert(!iss_native_apply_temporal_mode(&c,0) && c.percent==80);     // mode 0 leaves the percent
    assert(!iss_native_apply_temporal_mode(&c,5));
    f=tmpfile();assert(f);fputs("iss_temporal_mode=5\n",f);rewind(f);assert(!iss_native_config(f,&c));fclose(f);
    std::vector<unsigned char> image;fixture(image);const auto original=image;
    // Each instruction byte is independently corrupted. Even the last lighting
    // descriptor must refuse before a single dimension or HUD byte is changed.
    unsigned guards=0;
    for(const auto& s:iss_native_sites)for(unsigned byte=0;byte<s.length;byte++){
        image=original;image[s.offset+byte]^=1;auto before=image;
        assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{5,100,1}));assert(image==before);guards++;
    }
    for(unsigned byte=0;byte<sizeof(iss_native_projection);byte++){
        image=original;image[0x58030c+byte]^=1;auto before=image;
        assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{3,100,1}));assert(image==before);guards++;
    }
    for(unsigned byte=0;byte<sizeof(iss_native_aspect_width);byte++){
        image=original;image[0x5802e6+byte]^=1;auto before=image;
        assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{3,100,1}));assert(image==before);guards++;
    }
    // The map render target sites and the float's reader refuse the same way.
    for(const auto& s:iss_map_rt_sites)for(unsigned byte=0;byte<s.length;byte++){
        image=original;image[s.offset+byte]^=1;auto before=image;
        assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{5,100,1}));assert(image==before);guards++;
    }
    for(unsigned byte=0;byte<sizeof(iss_map_rt_float_load);byte++){
        image=original;image[ISS_MAP_RT_FLOAT_LOAD+byte]^=1;auto before=image;
        assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{5,100,1}));assert(image==before);guards++;
    }
    image=original;
    assert(!iss_native_apply(image.data(),image.size(),"CUSA00263","01.07",{3,100,1}));assert(image==original);
    assert(!iss_native_apply(image.data(),image.size(),"CUSA00004","01.00",{3,100,1}));assert(image==original);
    {auto eu=original;assert(iss_native_apply(eu.data(),eu.size(),"CUSA00004","01.07",{5,100,1})); /* EU 1.07 = same executable */
     assert(read32(eu.data()+0x1eff4a)==5120 && read32(eu.data()+0x1eff51)==1440);}
    assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.00",{3,100,1}));assert(image==original);
    assert(!iss_native_apply(image.data(),100,"CUSA00223","01.07",{3,100,1}));assert(image==original);
    assert(!iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{6,150,1}));assert(image==original);
    assert(iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{5,100,1}));
    assert(read32(image.data()+0x1eff4a)==5120 && read32(image.data()+0x1eff51)==1440);
    assert(read32(image.data()+0x1f0403)==3840 && read32(image.data()+0x1f03fb)==1080);
    assert(read32(image.data()+0x16be07)==3735552 && read32(image.data()+0x16be1f)==3735552);
    // Check every byte outside the patch plan, including the camera/karma sites.
    auto expected=original;IssNativeSite changes[ISS_NATIVE_SITE_COUNT];assert(iss_native_plan({5,100,1},&p));iss_native_changes(&p,changes);
    for(const auto& s:changes)memcpy(expected.data()+s.offset,s.after,s.length);
    iss_native_exposure_write(expected.data(),iss_native_exposure_entries(p.width,p.height));
    iss_map_rt_write(expected.data(),p.ui_width,p.ui_height);assert(image==expected);
    // Real-width canvas (5120x1440 = 3840x1080): the map target follows the canvas, not 1920x1080.
    {float f;memcpy(&f,image.data()+0xae5c9c,4);assert(f==3840.0f);}
    assert(read64(image.data()+0x5a611b)==((uint64_t)1080<<32|3840));
    assert(read32(image.data()+0x5a614b)==3840 && read32(image.data()+0x5a6181)==3841);
    assert(read32(image.data()+0x5a6189)==1080 && read32(image.data()+0x5a61a4)==1081);
    runtime_native_publish(-1,"Refused",nullptr);assert(runtime_native_status()==-1 && runtime_native_safe_area()==1.0f);
    p.hud_edges=0;runtime_native_publish(1,"Active",&p);assert(runtime_native_safe_area()==0.5f);
    p.hud_edges=1;runtime_native_publish(1,"Active",&p);assert(runtime_native_safe_area()==1.0f);
    // Separating the UI canvas must not narrow native world targets or its
    // camera aspect. Verify actual RIP-relative pointer destinations.
    for(int output:{3,5}){
        assert(iss_native_plan({output,100,0,1},&p));
        assert(p.ui_width==1920&&p.ui_height==1080&&p.width==iss_native_widths[output]&&p.height==1440);
        image=original;assert(iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{output,100,0,1}));
        const int64_t world_width=0x5802e6+7+int64_t(int32_t(read32(image.data()+0x5802e6+3)));
        const int64_t world_height=0x58030c+7+int64_t(int32_t(read32(image.data()+0x58030c+3)));
        assert(world_width==0x1eff4a&&world_height==0x1eff51);
        assert(read32(image.data()+world_width)==p.width&&read32(image.data()+world_height)==p.height);
        assert(read32(image.data()+0x1f0403)==1920&&read32(image.data()+0x1f03fb)==1080);
        for(const auto& m:iss_map_rt_sites)assert(!memcmp(image.data()+m.offset,m.before,m.length));
    }
    f=tmpfile();assert(f);fputs("iss_native_ui_16_9=1\n",f);rewind(f);assert(iss_native_config(f,&c)&&c.ui_16_9==1);fclose(f);
    for(int output:{1,2,3,4,5}){
        assert(iss_native_plan({output,100,0,2},&p));
        assert(p.ui_width*9==p.ui_height*16);
        assert(p.width==iss_native_widths[output]&&p.height==iss_native_heights[output]);
        if(output==3)assert(p.ui_width==2592&&p.ui_height==1458);
        if(output==5)assert(p.ui_width==3840&&p.ui_height==2160);
        image=original;assert(iss_native_apply(image.data(),image.size(),"CUSA00223","01.07",{output,100,0,2}));
        assert(read32(image.data()+0x1f0403)==p.ui_width&&read32(image.data()+0x1f03fb)==p.ui_height);
    }
    f=tmpfile();assert(f);fputs("iss_native_ui_16_9=2\n",f);rewind(f);assert(iss_native_config(f,&c)&&c.ui_16_9==2);fclose(f);
    f=tmpfile();assert(f);fputs("iss_native_ui_16_9=3\n",f);rewind(f);assert(!iss_native_config(f,&c));fclose(f);
    // Optional local-only executable validation: parse PT_LOAD segments instead
    // of assuming file offsets equal virtual addresses. No executable is shipped.
    /* Direct memory: retail up to 4K, more past it (7680x2160 crashed at 5056, ran at 6144). */
    assert(iss_native_direct_memory_mb(1920,1080)==ISS_NATIVE_DMEM_RETAIL_MB);
    assert(iss_native_direct_memory_mb(3840,2160)==ISS_NATIVE_DMEM_RETAIL_MB);
    assert(iss_native_direct_memory_mb(7680,2160)>=6144 && iss_native_direct_memory_mb(7680,2160)%256==0);
    assert(iss_native_direct_memory_mb(5120,2160)>ISS_NATIVE_DMEM_RETAIL_MB);
    assert(iss_native_direct_memory_mb(7680,4320)<=16384 && iss_native_direct_memory_mb(7680,4320)>iss_native_direct_memory_mb(7680,2160));
    assert(iss_native_direct_memory_mb(65535,65535)==16384);
    if(argc>1){
        std::ifstream stream(argv[1],std::ios::binary);assert(stream);
        std::vector<unsigned char> elf((std::istreambuf_iterator<char>(stream)),{});
        assert(elf.size()>64 && !memcmp(elf.data(),"\x7f" "ELF",4) && elf[4]==2);
        std::vector<unsigned char> mapped(0xb00000,0);
        uint64_t phoff=read64(elf.data()+32);uint16_t stride,n;memcpy(&stride,elf.data()+54,2);memcpy(&n,elf.data()+56,2);
        for(unsigned i=0;i<n;i++){
            auto at=phoff+uint64_t(i)*stride;assert(at+56<=elf.size());auto h=elf.data()+at;
            if(read32(h)!=1)continue;
            uint64_t off=read64(h+8),va=read64(h+16),size=read64(h+32);
            assert(off+size<=elf.size());if(va>=mapped.size())continue;
            size=std::min<uint64_t>(size,mapped.size()-va);memcpy(mapped.data()+va,elf.data()+off,size);
        }
        for(int output=1;output<ISS_NATIVE_PRESETS;output++){
            auto copy=mapped;assert(iss_native_apply(copy.data(),copy.size(),"CUSA00223","01.07",{output,100,1}));
        }
        puts("Actual US ELF: all guarded patch plans accepted (no disk writes).");
    }
    printf("Native resolution: exact dimensions, scale bounds, Hor+ math, lighting capacity, %u corrupted signature bytes, atomic refusal and safe-area fallback passed.\n",guards);
}
