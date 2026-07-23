#include "rv_pccm.hpp"

#include "pdk/rv_err.hpp"

// Operations return RV_ERR_IO until the backend lands (stage 6).
int64_t rv_3dmppc::rv_pccm::card_slots() { return conf_.card_slots; }

int64_t rv_3dmppc::rv_pccm::card_slot_size() { return conf_.card_slot_size; }

int64_t rv_3dmppc::rv_pccm::card_size(int64_t) { return RV_ERR_IO; }

int64_t rv_3dmppc::rv_pccm::card_read(int64_t, void*, int64_t) {
    return RV_ERR_IO;
}

int64_t rv_3dmppc::rv_pccm::card_write(int64_t, const void*, int64_t) { return RV_ERR_IO; }

int64_t rv_3dmppc::rv_pccm::card_erase(int64_t) { return RV_ERR_IO; }
