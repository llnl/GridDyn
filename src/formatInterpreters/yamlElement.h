#pragma once

/*
 * LLNS Copyright Start
 * Copyright (c) 2017, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "yaml-cpp/yaml.h"
#include <string>

class yamlElement {
  public:
    std::string name;  //!< the name of the element
    int elementIndex = 0;  //!< an indicator of element Index
    unsigned int arrayIndex = 0;  //!< the current index into a sequence
    yamlElement() {}
    yamlElement(const YAML::Node& vElement, std::string newName);

    /** reset the element*/
    void clear();

    const YAML::Node getElement() const;
    size_t count() const { return (arraytype) ? element.size() : ((element) ? 0 : 1); }
    bool isNull() const
    {
        if ((element) && (element.IsDefined())) {
            return (arraytype) ? false : element.IsNull();
        }
        return true;
    }

  private:
    YAML::Node element;  //!< pointer to the actual YAML element
    bool arraytype = false;  //!< indicator if the element is a sequence
};
