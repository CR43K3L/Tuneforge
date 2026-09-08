#pragma once
//
// Plans d'alimentation Windows via l'API powrprof (pas de shell-out sur powercfg.exe).
//
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "common/util.hpp"

namespace tf::power {

// --- GUID de plans ---------------------------------------------------------
inline constexpr GUID kSchemeBalanced =
    {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
inline constexpr GUID kSchemeHighPerf =
    {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
inline constexpr GUID kSchemeUltimate =
    {0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};

// --- Sous-groupes ----------------------------------------------------------
inline constexpr GUID kSubProcessor =
    {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
inline constexpr GUID kSubUsb =
    {0x2a737441, 0x1930, 0x4402, {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
inline constexpr GUID kSubPciExpress =
    {0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
inline constexpr GUID kSubDisk =
    {0x0012ee47, 0x9041, 0x4b5d, {0x9b, 0x77, 0x53, 0x5f, 0xba, 0x8b, 0x14, 0x42}};

// --- Reglages --------------------------------------------------------------
// Coeurs minimum/maximum non "parkes" (0-100 %).
inline constexpr GUID kSetCoreParkingMin =
    {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
inline constexpr GUID kSetCoreParkingMax =
    {0xea062031, 0x0e34, 0x4ff1, {0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x28}};
// Mode de boost du processeur : 0 Desactive, 1 Active, 2 Agressif, ...
inline constexpr GUID kSetPerfBoostMode =
    {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
// Etat processeur minimum / maximum (%).
inline constexpr GUID kSetProcThrottleMin =
    {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
inline constexpr GUID kSetProcThrottleMax =
    {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
// Suspension selective USB : 0 Desactive, 1 Active.
inline constexpr GUID kSetUsbSelectiveSuspend =
    {0x48e6b7a6, 0x50f5, 0x4782, {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};
// Gestion d'alimentation des liens PCIe : 0 Off, 1 Economie moderee, 2 maximale.
inline constexpr GUID kSetPciExpressAspm =
    {0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
// Delai d'arret des disques durs (secondes, 0 = jamais).
inline constexpr GUID kSetDiskTimeout =
    {0x6738e2c4, 0xe8a5, 0x4a42, {0xb1, 0x6a, 0xe0, 0x40, 0xe7, 0x69, 0x75, 0x6e}};

// --- API -------------------------------------------------------------------
std::string guid_to_string(const GUID& g);
std::optional<GUID> guid_from_string(const std::string& s);
bool guid_equal(const GUID& a, const GUID& b);

std::optional<GUID> active_scheme();
Result              set_active_scheme(const GUID& scheme);
std::string         scheme_name(const GUID& scheme);
std::vector<GUID>   list_schemes();
bool                scheme_exists(const GUID& scheme);

// Cree le plan « Performances ultimes » s'il est absent (il est masque par
// defaut sur beaucoup d'installations). Renvoie son GUID reel.
std::optional<GUID> ensure_ultimate_scheme();

// Lecture/ecriture sur le plan actif (ou un plan explicite).
std::optional<uint32_t> read_ac(const GUID& sub, const GUID& setting,
                                const GUID* scheme = nullptr);
std::optional<uint32_t> read_dc(const GUID& sub, const GUID& setting,
                                const GUID* scheme = nullptr);
Result write_ac(const GUID& sub, const GUID& setting, uint32_t value,
                const GUID* scheme = nullptr);
Result write_dc(const GUID& sub, const GUID& setting, uint32_t value,
                const GUID* scheme = nullptr);

// Reapplique le plan actif : necessaire pour que les ecritures prennent effet.
Result commit();

// Rend visible un reglage masque dans l'UI Windows (Attributes = 2).
void unhide_setting(const GUID& sub, const GUID& setting);

} // namespace tf::power
