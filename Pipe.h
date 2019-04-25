#pragma once
#include <string>
class Pipe
{
public:
	Pipe() {};
	Pipe(const std::string& pipeName, size_t size = 65536 * 128, bool blocking = false)
	{
		setup(pipeName, size, blocking);
	}
	~Pipe() {cleanup();}
	bool setup(const std::string& pipeName = defaultName, size_t size = 65536 * 128, bool blocking = false);
	void cleanup();
	void setRtBlocking(bool blocking);

	template<typename T> bool writeNonRt(const T& data);
	template<typename T> bool writeNonRt(T* ptr, size_t count);
	template<typename T> bool writeRt(const T& data);
	template<typename T> bool writeRt(T* ptr, size_t count);

	template<typename T> ssize_t readNonRt(T & dest);
	template<typename T> ssize_t readRt(T & dest);
	template<typename T> ssize_t readNonRt(T* dest, size_t count);
	template<typename T> ssize_t readRt(T* dest, size_t count);
private:
	bool _writeNonRt(void* ptr, size_t size);
	bool _writeRt(void* ptr, size_t size);
	ssize_t _readNonRt(void* ptr, size_t size);
	ssize_t _readRt(void* ptr, size_t size);
	static std::string defaultName;
	std::string name;
	std::string path;
	int pipeSocket;
	int fd;
	int pipeSize;
};

template<typename T> bool Pipe::writeNonRt(const T& data) 
{
	return writeNonRt(&data, 1);
}

template <typename T> bool Pipe::writeNonRt(T* data, size_t count)
{
	size_t size = count * sizeof(*data);
	return _writeNonRt((void*)data, size);
}

template<typename T> bool Pipe::writeRt(const T& data) 
{
	return writeRt(&data, 1);
}

template <typename T> bool Pipe::writeRt(T* ptr, size_t count)
{
	size_t size = count * sizeof(*ptr);
	return _writeRt((void*)ptr, size);
}

template<typename T> ssize_t Pipe::readRt(T & dest)
{
	return readRt(&dest, 1);
}

template<typename T> ssize_t Pipe::readRt(T* dest, size_t count)
{
	ssize_t ret = _readRt((void*)dest, count * sizeof(*dest));
	if(ret >= 0)
		return ret / sizeof(*dest);
	else
		return ret;
}

template<typename T> ssize_t Pipe::readNonRt(T & dest)
{
	return readNonRt(&dest, 1);
}

template<typename T> ssize_t Pipe::readNonRt(T* dest, size_t count)
{
	ssize_t ret = _readNonRt((void*)dest, count * sizeof(*dest));
	if(ret >= 0)
		return ret / sizeof(*dest);
	else
		return ret;
}
