
template <typename T>
std::string toString(const T& value)
{
	std::stringstream stream;
	stream << value;
	return stream.str();
}

template <typename T>
int toInt(T str)
{
	int res;
	std::stringstream stream;
	stream << str;
	stream >> res;
	return res;
}
