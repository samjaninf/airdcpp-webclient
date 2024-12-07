/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"
#include <airdcpp/protocol/AdcCommand.h>

#include <airdcpp/user/CID.h>
#include <airdcpp/util/Util.h>

namespace dcpp {

AdcCommand::AdcCommand(uint32_t aCmd, char aType /* = TYPE_CLIENT */) noexcept : cmdInt(aCmd), type(aType) { }
AdcCommand::AdcCommand(uint32_t aCmd, dcpp::SID aTarget, char aType) noexcept : cmdInt(aCmd), to(aTarget), type(aType) { }
AdcCommand::AdcCommand(Severity sev, Error err, const string& desc, char aType /* = TYPE_CLIENT */) noexcept : cmdInt(CMD_STA), type(aType) {
	addParam((sev == SEV_SUCCESS && err == SUCCESS) ? "000" : Util::toString(sev * 100 + err));
	addParam(desc);
}

AdcCommand::AdcCommand(const string& aLine, bool nmdc /* = false */) : cmdInt(0), type(TYPE_CLIENT) {
	parse(aLine, nmdc);
}

bool AdcCommand::isValidType(char aType) noexcept {
	if (aType != TYPE_BROADCAST && aType != TYPE_CLIENT && aType != TYPE_DIRECT && aType != TYPE_ECHO && aType != TYPE_FEATURE && aType != TYPE_INFO && aType != TYPE_HUB && aType != TYPE_UDP) {
		return false;
	}

	return true;
}

void AdcCommand::parse(const string& aLine, bool nmdc /* = false */) {
	string::size_type i = 5;

	if(nmdc) {
		// "$ADCxxx ..."
		if(aLine.length() < 7)
			throw ParseException("Too short");
		type = TYPE_CLIENT;
		cmd[0] = aLine[4];
		cmd[1] = aLine[5];
		cmd[2] = aLine[6];
		i += 3;
	} else {
		// "yxxx ..."
		if(aLine.length() < 4)
			throw ParseException("Too short");
		type = aLine[0];
		cmd[0] = aLine[1];
		cmd[1] = aLine[2];
		cmd[2] = aLine[3];
	}

	if(!isValidType(type)) {
		throw ParseException("Invalid type");
	}

	if(type == TYPE_INFO) {
		from = HUB_SID;
	}

	string::size_type len = aLine.length();
	const char* buf = aLine.c_str();
	string cur;
	cur.reserve(128);

	bool toSet = false;
	bool featureSet = false;
	bool fromSet = nmdc; // $ADCxxx never have a from CID...

	while(i < len) {
		switch(buf[i]) {
		case '\\':
			++i;
			if(i == len)
				throw ParseException("Escape at eol");
			if(buf[i] == 's')
				cur += ' ';
			else if(buf[i] == 'n')
				cur += '\n';
			else if(buf[i] == '\\')
				cur += '\\';
			else if(buf[i] == ' ' && nmdc)	// $ADCGET escaping, leftover from old specs
				cur += ' ';
			else
				throw ParseException("Unknown escape");
			break;
		case ' ': 
			// New parameter...
			{
				if((type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) && !fromSet) {
					if(cur.length() != 4) {
						throw ParseException("Invalid SID length");
					}
					from = toSID(cur);
					fromSet = true;
				} else if((type == TYPE_DIRECT || type == TYPE_ECHO) && !toSet) {
					if(cur.length() != 4) {
						throw ParseException("Invalid SID length");
					}
					to = toSID(cur);
					toSet = true;
				} else if(type == TYPE_FEATURE && !featureSet) {
					if(cur.length() % 5 != 0) {
						throw ParseException("Invalid feature length");
					}
					// Skip...
					featureSet = true;
				} else {
					parameters.push_back(cur);
				}
				cur.clear();
			}
			break;
		default:
			cur += buf[i];
		}
		++i;
	}
	if(!cur.empty()) {
		if((type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) && !fromSet) {
			if(cur.length() != 4) {
				throw ParseException("Invalid SID length");
			}
			from = toSID(cur);
			fromSet = true;
		} else if((type == TYPE_DIRECT || type == TYPE_ECHO) && !toSet) {
			if(cur.length() != 4) {
				throw ParseException("Invalid SID length");
			}
			to = toSID(cur);
			toSet = true;
		} else if(type == TYPE_FEATURE && !featureSet) {
			if(cur.length() % 5 != 0) {
				throw ParseException("Invalid feature length");
			}
			// Skip...
			featureSet = true;
		} else {
			parameters.push_back(cur);
		}
	}

	if((type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) && !fromSet) {
		throw ParseException("Missing from_sid");
	}
	
	if(type == TYPE_FEATURE && !featureSet) {
		throw ParseException("Missing feature");
	}
	
	if((type == TYPE_DIRECT || type == TYPE_ECHO) && !toSet) {
		throw ParseException("Missing to_sid");
	}
}

AdcCommand& AdcCommand::addFeature(const string& feat, FeatureType aType) noexcept {
	features += aType == FeatureType::REQUIRED ? "+" : "-";
	features += feat;
	return *this; 
}

string AdcCommand::toString(const CID& aCID) const noexcept {
	return getHeaderString(aCID) + getParamString(false);
}

string AdcCommand::toString() const noexcept {
	return getHeaderString() + getParamString(false);
}

string AdcCommand::toString(dcpp::SID sid /* = 0 */, bool nmdc /* = false */) const noexcept {
	return getHeaderString(sid, nmdc) + getParamString(nmdc);
}

string AdcCommand::escape(const string& str, bool old) noexcept {
	string tmp = str;
	string::size_type i = 0;
	while( (i = tmp.find_first_of(" \n\\", i)) != string::npos) {
		if(old) {
			tmp.insert(i, "\\");
		} else {
			switch(tmp[i]) {
				case ' ': tmp.replace(i, 1, "\\s"); break;
				case '\n': tmp.replace(i, 1, "\\n"); break;
				case '\\': tmp.replace(i, 1, "\\\\"); break;
			}
		}
		i+=2;
	}
	return tmp;
}

string AdcCommand::getHeaderString(dcpp::SID sid, bool nmdc) const noexcept {
	string tmp;
	if(nmdc) {
		tmp += "$ADC";
	} else {
		tmp += getType();
	}

	tmp += cmdChar;

	if(type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) {
		tmp += ' ';
		tmp += fromSID(sid);
	}

	if(type == TYPE_DIRECT || type == TYPE_ECHO) {
		tmp += ' ';
		tmp += fromSID(to);
	}

	if(type == TYPE_FEATURE) {
		tmp += ' ';
		tmp += features;
	}
	return tmp;
}

string AdcCommand::getHeaderString(const CID& cid) const noexcept {
	dcassert(type == TYPE_UDP);
	string tmp;
	
	tmp += getType();
	tmp += cmdChar;
	tmp += ' ';
	tmp += cid.toBase32();
	return tmp;
}

string AdcCommand::getHeaderString() const noexcept {
	dcassert(type == TYPE_UDP);
	string tmp;
	
	tmp += getType();
	tmp += cmdChar;
	return tmp;
}

AdcCommand& AdcCommand::addParams(const ParamMap& aParams) noexcept {
	for (const auto& [name, value] : aParams) {
		addParam(name, value);
	}
	return *this;
}

const string& AdcCommand::getParam(size_t n) const noexcept {
	return getParameters().size() > n ? getParameters()[n] : Util::emptyString;
}

string AdcCommand::getParamString(bool nmdc) const noexcept {
	string tmp;
	for(const auto& i: getParameters()) {
		tmp += ' ';
		tmp += escape(i, nmdc);
	}
	if(nmdc) {
		tmp += '|';
	} else {
		tmp += '\n';
	}
	return tmp;
}

bool AdcCommand::getParam(const char* name, size_t start, string& ret) const noexcept {
	for(string::size_type i = start; i < getParameters().size(); ++i) {
		if(toCode(name) == toCode(getParameters()[i].c_str())) {
			ret = getParameters()[i].substr(2);
			return true;
		}
	}
	return false;
}

bool AdcCommand::getParam(const char* name, size_t start, StringList& ret) const noexcept {
	for(string::size_type i = start; i < getParameters().size(); ++i) {
		if(toCode(name) == toCode(getParameters()[i].c_str())) {
			ret.push_back(getParameters()[i].substr(2));
		}
	}
	return !ret.empty();
}

bool AdcCommand::hasFlag(const char* name, size_t start) const noexcept {
	for(string::size_type i = start; i < getParameters().size(); ++i) {
		if(toCode(name) == toCode(getParameters()[i].c_str()) && 
			getParameters()[i].size() == 3 &&
			getParameters()[i][2] == '1')
		{
			return true;
		}
	}
	return false;
}

AdcCommand::CommandType AdcCommand::toCommand(const string& aCmd) noexcept {
	dcassert(aCmd.length() == 3);
	return (((uint32_t)aCmd[0]) | (((uint32_t)aCmd[1]) << 8) | (((uint32_t)aCmd[2]) << 16));
}

string AdcCommand::fromCommand(CommandType x) noexcept {
	string tmp(3, 0); 

	uint8_t out[4];
	*(uint32_t*)&out = x;

	tmp[0] = out[0]; tmp[1] = out[1]; tmp[2] = out[2];
	return tmp; 
}

} // namespace dcpp