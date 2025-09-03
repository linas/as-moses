/*
 * moses/modes/representation/instance.h
 *
 * Copyright (C) 2002-2008 Novamente LLC
 * All Rights Reserved
 *
 * Written by Moshe Looks
 *            Predrag Janicic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _REPRESENTATION_INSTANCE_H
#define _REPRESENTATION_INSTANCE_H

#include <opencog/asmoses/utils/tree.h>

namespace opencog {
namespace moses {

// Storage types for packed populations.
typedef unsigned long int packed_t;
#define bits_per_packed_t (8*sizeof(packed_t))

// Value types accessing unpacked instances.
typedef double       contin_t;  // continuous
typedef unsigned     disc_t;    // discrete
typedef std::string  term_t;
typedef tree<term_t> term_tree;

typedef std::vector<packed_t> instance;

} // ~namespace moses
} // ~namespace opencog

// This is to enable std::unordered_map and similar
namespace std
{
	template<>
	struct hash<opencog::moses::instance>
	{
		size_t operator()(const opencog::moses::instance& nstc) const noexcept
		{
			size_t hsh = 0;
			for (unsigned long int bs: nstc)
				hsh ^= std::hash<unsigned long int>{}(bs)
					+ 0x9e3779b9 + (hsh << 6) + (hsh >> 2);
			return hsh;
		}
	};
}

#endif
