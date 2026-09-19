#pragma once

namespace Ag
{
	// Text: append, compare, search, case, substring, replace, and converting numbers, booleans and vectors to and from text.
	// Strings are interned (see StringRegistry): every distinct result is kept for the whole run, so building text from
	// a value that never repeats (a running timer) grows memory. Format such text once, not every frame.
	void RegisterStringNodes();
} // namespace Ag
