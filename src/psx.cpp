#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <fmt/format.h>

#include "sfo.hpp"
#include "psx.hpp"
#include "log.hpp"

static std::map<std::string, std::string> installed_psx_games;


bool pkgi_is_psx_game_installed_titleid(std::string title_id) {
    return (installed_psx_games.find(title_id) != installed_psx_games.end());
}

void pkgi_psx_add_installed_game(std::string title_id, std::string disc_id) {
    installed_psx_games.insert({title_id, disc_id});
}

std::string pkgi_pbp_read_disc_id(std::string eboot_pbp) {
    pbp_header pbp_hdr;
    
    SceUID fd = sceIoOpen(eboot_pbp.c_str(), SCE_O_RDONLY, 0777);    
    
    std::string disc_id = "";
    // check can open eboot file at all
    if(fd >= 0) {
        // check file is atleast size of pbp header
        SceSSize read_sz = sceIoRead(fd, &pbp_hdr, sizeof(pbp_header));
        if(read_sz >= static_cast<SceSSize>(sizeof(pbp_header))) {
            // check magic is PBP magic
            if(memcmp(pbp_hdr.magic, "\0PBP", sizeof(pbp_hdr.magic)) == 0){
                // check the param.sfo length is valid
                if(pbp_hdr.icon0_png > pbp_hdr.param_sfo) {
                    sceIoLseek(fd, pbp_hdr.param_sfo, SCE_SEEK_SET);
                    size_t sfo_sz = pbp_hdr.icon0_png - pbp_hdr.param_sfo;
                    
                    // read the sfo from the pbp
                    uint8_t* sfo = new uint8_t[sfo_sz];
                    read_sz = sceIoRead(fd, sfo, sfo_sz);
                    
                    // check read size is actually the size of the sfo
                    if(read_sz >= 0 && static_cast<size_t>(read_sz) >= sfo_sz) {
                        disc_id = pkgi_sfo_get_string(sfo, sfo_sz, "DISC_ID");
                    }
                    
                    // free the sfo buffer
                    if(sfo != nullptr) {
                        delete[] sfo;
                    }
                }
            }
        }
        sceIoClose(fd);
    }
    return disc_id;
}

void pkgi_process_pbp(std::string eboot_pbp, std::string disc_id) {
    pbp_header pbp_hdr;
    
    SceUID fd = sceIoOpen(eboot_pbp.c_str(), SCE_O_RDONLY, 0777);   
    // check can open eboot file at all
    if(fd >= 0) {
        // check file is atleast size of pbp header
        SceSSize read_sz = sceIoRead(fd, &pbp_hdr, sizeof(pbp_header));
        if(read_sz >= static_cast<SceSSize>(sizeof(pbp_header))) {
            // check magic is PBP magic
            if(memcmp(pbp_hdr.magic, "\0PBP", sizeof(pbp_hdr.magic)) == 0){
                sceIoLseek(fd, pbp_hdr.data_psar, SCE_SEEK_SET);
                LOGF("Valid PBP header magic found");
                
                // check DATA.PSAR is atleast 8 bytes long
                char magic[0x8];
                read_sz = sceIoRead(fd, magic, sizeof(magic));
                if(read_sz >= static_cast<SceSSize>(sizeof(magic))) {
                    
                    // check is psx game
                    if(memcmp(magic, "PSISOIMG", sizeof(magic)) == 0 || memcmp(magic, "PSTITLEI", sizeof(magic)) == 0) {
                        LOGF("PSAR magic: {}", std::string(magic, 8));
                        
                        // check data.psp size is atleast the data.psp self header
                        np_data_psp data_psp;
                        sceIoLseek(fd, pbp_hdr.data_psp, SCE_SEEK_SET);
                        read_sz = sceIoRead(fd, &data_psp, sizeof(np_data_psp));
                        if(read_sz >= static_cast<SceSSize>(sizeof(np_data_psp))) {
                            // check the content id is the correct length
                            int cid_sz = strnlen(data_psp.content_id, 40);
                            if(cid_sz == 36) {
                                std::string title_id = std::string(data_psp.content_id + 7, 9);
                                pkgi_psx_add_installed_game(title_id, disc_id);
                                LOGF("Mapped PSX title ID {} to disc ID {}", title_id, disc_id);
                            }
                        }
                    }
                }
                
            }
        }
        
        sceIoClose(fd);
    }
    
}

void pkgi_scan_pbps() {
    std::string parent_folder = "ux0:/pspemu/PSP/GAME";
    SceUID dfd = sceIoDopen(parent_folder.c_str());
    
    std::string eboot_file;
    
    int dir_read_ret = 0;
    SceIoDirent dir;    
    do{
        memset(&dir, 0x00, sizeof(SceIoDirent));
        dir_read_ret = sceIoDread(dfd, &dir);    

        std::string disc_id = std::string(dir.d_name);
        std::string eboot_file = fmt::format("{}/{}/EBOOT.PBP", parent_folder, disc_id);
        pkgi_process_pbp(eboot_file, disc_id);
        
        LOGF("Scanning PSX game folder: {}", eboot_file);
        
    } while(dir_read_ret > 0);
    
    sceIoDclose(dfd);
    
}
