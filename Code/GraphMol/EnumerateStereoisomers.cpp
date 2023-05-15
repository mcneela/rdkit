#include "EnumerateStereoisomers.h"

namespace RDKit {
    _BondFlipper::_BondFlipper(Bond *bond) :
        bond(bond) {};

    void _BondFlipper::flip(bool flag) {
        if (flag) {
            bond->setStereo(Bond::STEREOCIS);
        } else {
            bond->setStereo(Bond::STEREOTRANS);
        }
    };

    _AtomFlipper::_AtomFlipper(Atom* atom) {
        atom = atom;
    };

    void _AtomFlipper::flip(bool flag) {
        if (flag) {
            atom->setChiralTag(Atom::CHI_TETRAHEDRAL_CW);
        } else {
            atom->setChiralTag(Atom::CHI_TETRAHEDRAL_CCW);
        }
    };

    _StereoGroupFlipper::_StereoGroupFlipper(RDKit::StereoGroup* group) {
        _original_parities = std::vector<std::tuple<Atom*, RDKit::Atom::ChiralType> >();
        for (Atom* atom : group->getAtoms()) {
            _original_parities.push_back(std::make_tuple(atom, atom->getChiralTag()));
        }
    };

    void _StereoGroupFlipper::flip(bool flag) {
        if (flag) {
            for (auto& atom_parity : _original_parities) {
                std::get<0>(atom_parity)->setChiralTag(std::get<1>(atom_parity));
            } 
        } else {
            for (auto& atom_parity : _original_parities) {
                if (std::get<1>(atom_parity) == Atom::CHI_TETRAHEDRAL_CW) {
                    std::get<0>(atom_parity)->setChiralTag(Atom::CHI_TETRAHEDRAL_CCW);
                } else if (std::get<1>(atom_parity) == Atom::CHI_TETRAHEDRAL_CCW) {
                    std::get<0>(atom_parity)->setChiralTag(Atom::CHI_TETRAHEDRAL_CW);
                }
            }
        }
    };

    std::vector<_Flipper*> _get_flippers(ROMol* mol, const StereoEnumerationOptions options) {
        std::vector<RDKit::Chirality::StereoInfo> potential_stereo = RDKit::Chirality::findPotentialStereo(*mol);

        std::vector<_Flipper*> flippers = std::vector<_Flipper*>();
        if (!options.only_stereo_groups) {
            for (auto atom : mol->atoms()) {
                if (atom->hasProp("_ChiralityPossible")) {
                    if (!options.only_unassigned || atom->getChiralTag() == Atom::CHI_UNSPECIFIED) {
                        flippers.push_back(new _AtomFlipper(atom));
                    }
                }
            }
            
            for (auto bond : mol->bonds()) {
                Bond::BondStereo bstereo = bond->getStereo();
                if (bstereo != Bond::STEREONONE) {
                    if (!options.only_unassigned || bstereo == Bond::STEREOANY) {
                        flippers.push_back(new _BondFlipper(bond));
                    }
                }
            }
        } 

        if (options.only_unassigned) {
            for (auto group : mol->getStereoGroups()) {
                if (group.getGroupType() != StereoGroupType::STEREO_ABSOLUTE) {
                    flippers.push_back(new _StereoGroupFlipper(&group));
                }
            }
        }
        return flippers;
    };
    
    unsigned int get_stereoisomer_count(ROMol* mol, const StereoEnumerationOptions options=StereoEnumerationOptions()) {
        std::vector<_Flipper*> flippers = _get_flippers(mol, options);
        return std::pow(2, flippers.size());
    }

    std::vector<ROMol*> enumerate_stereoisomers(ROMol* mol, const StereoEnumerationOptions options=StereoEnumerationOptions(), bool verbose=false) {
        for (auto atom : mol->atoms()) {
            atom->clearProp("_CIPCode");
        }
        for (auto bond : mol->bonds()) {
            if (bond->getBondDir() == Bond::BondDir::EITHERDOUBLE) {
                bond->setBondDir(Bond::BondDir::NONE);
            }
        }
        auto flippers = _get_flippers(mol, options);
        const unsigned int n_centers = flippers.size();

        if (!n_centers) {
            return std::vector<ROMol*>{mol};
        }

        if (options.max_isomers == 0 || std::pow(2, n_centers) <= options.max_isomers) {
            std::vector<unsigned int> bitsource = _RangeBitsGenerator(n_centers);
        } else {
            if (!options.rand) {
                // deterministic random seed invariant to input atom order
                std::vector<std::pair<int, int> > ordered_atoms;
                for (auto atom : mol->atoms()) {
                    ordered_atoms.push_back(std::make_pair(atom->getDegree(), atom->getAtomicNum()));
                }
                std::sort(ordered_atoms.begin(), ordered_atoms.end());
                std::size_t seed = std::hash<std::vector<std::pair<int, int>>>(ordered_atoms);
                rand = srand(seed);
            } else {
                rand = srand(options.rand);
            }
        }

        bitsource = _UniqueRandomBitsGenerator(n_centers, options.max_isomers, rand);

        std::set<std::string> seen_isomers;
        int num_isomers = 0;
        for (auto bitflag : bitsource) {
            for (int i = 0; i < n_centers; ++i) {
                flippers[i]->flip(bitflag & (1 << i));
            }

            RWMol* isomer;
            if (mol->getStereoGroups()) {
                std::vector<StereoGroup> empty_group;
                isomer = new RWMol(*mol);
                isomer->setStereoGroups(std::move(empty_group));

            } else {
                isomer = new RWMol(*mol);
            }
            MolOps::setDoubleBondNeighborDirections(*isomer);
            isomer->clearComputedProps(false);

            MolOps::assignStereochemistry(*isomer, true, true, true);
            if (options.unique) {
                std::string cansmi = MolToSmiles(*isomer, true);
                if (seen_isomers.find(cansmi) != seen_isomers.end()) {
                    continue;
                }

                seen_isomers.insert(cansmi);
            }

            if (options.try_embedding) {
                MolOps::addHs(*isomer);
                DGeomHelpers::EmbedMolecule(*isomer, bitflag & 0x7fffffff);

            }
        }

    }
}