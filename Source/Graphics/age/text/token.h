#ifndef _TOKEN_H_
#define _TOKEN_H_

#include <string>

//===============================================================
class Token
{
//---------------------------------------------------------------
// Members
private:

protected:

public:
	int				m_tokenID;
	int				m_matchPos;
	std::string		m_tokenAsStr;

//---------------------------------------------------------------
// Methods
private:

protected:

public:
						Token(int tokenID, std::string tokenAsStr);
	virtual				~Token();
};

#endif // _TOKEN_H_
