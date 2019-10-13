#pragma once

#include <array>
#include <memory>
#include "../concept.h"

namespace fast_io::crypto
{

template <input_stream T, typename Enc>
class basic_ictr
{
public:
    using native_interface_t = T;
    using char_type = typename native_interface_t::char_type;
    using cipher_type = Enc;
private:
    using unsigned_char_type = std::make_unsigned_t<char_type>;
public:
    using key_type = std::array<unsigned_char_type, cipher_type::key_size>;
    using block_type = std::array<unsigned_char_type, cipher_type::block_size>;
    using nonce_type = std::array<unsigned_char_type, cipher_type::block_size - sizeof(std::size_t)>;

private:
	using block_iterator = typename block_type::iterator;
    block_type cipher_buf = {};
	block_iterator cipher_buf_pos = cipher_buf.begin();
    block_type plaintext_buf = {};
	block_iterator plaintext_buf_pos = plaintext_buf.begin();
    key_type key;
    nonce_type nonce;
    std::size_t char_counter;
    T ib;
    Enc enc;

    unsigned_char_type *mread(unsigned_char_type *pb, unsigned_char_type *pe)
    {
        auto pi(pb);

        std::size_t const input_length(pe - pi);

        if (plaintext_buf_pos != plaintext_buf.begin())
        {
            std::size_t buffer_length(plaintext_buf_pos - plaintext_buf.begin());
            std::size_t min_length(input_length);
            if (buffer_length < min_length)
                min_length = buffer_length;
			auto const buf_it(plaintext_buf.begin() + min_length);
            pi = std::uninitialized_copy(plaintext_buf.begin(), plaintext_buf.begin() + min_length, pi);
            std::size_t plaintext_remain_length(plaintext_buf_pos - plaintext_buf.begin() - min_length);
            if (plaintext_remain_length)
            {
                std::uninitialized_copy(buf_it, buf_it + plaintext_remain_length, plaintext_buf.begin());
                plaintext_buf_pos = plaintext_buf.begin() + plaintext_remain_length;
                return pi;
            }
            plaintext_buf_pos = plaintext_buf.begin();
        }

        for (; pi != pe;)
        {
            auto old_pos = cipher_buf_pos;
            cipher_buf_pos = ib.reads(cipher_buf_pos, cipher_buf.end());
            char_counter += cipher_buf_pos - old_pos;
            if (cipher_buf_pos != cipher_buf.end())
                return pi;

            block_type block;
            std::size_t block_counter(char_counter / cipher_type::block_size - 1);
            memcpy(block.data(), nonce.data(), cipher_type::block_size - sizeof(std::size_t));
            // TODO: big-endian or small-endian
            memcpy(block.data() + cipher_type::block_size - sizeof(std::size_t), &block_counter, sizeof(std::size_t));
            auto plain(enc(block.data()));
            for (std::size_t i(0); i != plain.size(); ++i)
                plain[i] ^= cipher_buf[i];

            cipher_buf_pos = cipher_buf.begin();

            std::size_t available_out_space(pe - pi);
            if (available_out_space < cipher_type::block_size)
            {
                pi = std::uninitialized_copy(plain.begin(), plain.begin() + available_out_space, pi);
                plaintext_buf_pos = std::uninitialized_copy(plain.begin() + available_out_space, plain.end(), plaintext_buf.begin());
                break;
            }
            else
            {
                pi = std::uninitialized_copy(plain.begin(), plain.end(), pi);
            }

        }
        return pi;
    }

public:
    template<typename T1, typename T2,typename ...Args>
	requires std::constructible_from<key_type, T1> && std::constructible_from<nonce_type, T2> && std::constructible_from<T,Args...>
    basic_ictr(T1&& init_key, T2&& nonce, Args &&...args) : key(std::forward<T1>(init_key)), nonce(std::forward<T2>(nonce)), char_counter(0), ib(std::forward<Args>(args)...), enc(key.data())
    {
    }
    template<std::contiguous_iterator Iter>
	Iter reads(Iter begin, Iter end)
    {
        auto bgchadd(static_cast<unsigned_char_type *>(static_cast<void *>(std::to_address(begin))));
        return begin + (mread(bgchadd, static_cast<unsigned_char_type *>(static_cast<void *>(std::to_address(end)))) - bgchadd) / sizeof(*begin);
    }

    char_type get()
	{
        if (plaintext_buf_pos == plaintext_buf.begin())
        {
            block_type tmp;
            auto next_ch(tmp.begin() + 1);
            auto ret(reads(tmp.begin(), next_ch));
            if (ret != next_ch)
                throw eof();
            return static_cast<char_type>(*tmp.begin());
        }
        auto ch(*plaintext_buf_pos);
        if (plaintext_buf_pos == plaintext_buf.end())
            plaintext_buf_pos = plaintext_buf.begin();
        else
            ++plaintext_buf_pos;
        return static_cast<char_type>(ch);
	}

    std::pair<char_type, bool> try_get()
	{
        if (plaintext_buf_pos == plaintext_buf.begin())
        {
            block_type tmp;
            auto next_ch(tmp.begin() + 1);
            auto ret(reads(tmp.begin(), next_ch));
            if (ret != next_ch)
                return {0, true};
            return {static_cast<char_type>(*tmp.begin()), false};
        }
        auto ch(*plaintext_buf_pos);
        if (plaintext_buf_pos == plaintext_buf.end())
            plaintext_buf_pos = plaintext_buf.begin();
        else
            ++plaintext_buf_pos;
        return {static_cast<char_type>(ch), false};
	}
public:
	template<typename ...Args>
	auto seek(Args&& ...args) requires(random_access_stream<T>)
	{
        auto ret(ib.seek(std::forward<Args>(args)...));
        std::size_t pos_rel_to_begin = ret;
        std::size_t counter_pos(pos_rel_to_begin / cipher_type::block_size);
		std::size_t char_pos_block_aligned = counter_pos * cipher_type::block_size;
        if (pos_rel_to_begin == char_pos_block_aligned)
        {
            cipher_buf_pos = cipher_buf.begin();
            plaintext_buf_pos = plaintext_buf.begin();
            char_counter = pos_rel_to_begin;
            return ret;
        }
        block_type tmp;
        std::size_t read_length(pos_rel_to_begin - char_pos_block_aligned);
		auto const needreed = tmp.data() + read_length;
		ib.seek(char_pos_block_aligned, seekdir::beg);
        auto tmp_pos(ib.reads(tmp.data(), needreed));
        if (tmp_pos != needreed)
            throw eof();
        cipher_buf = tmp;
        cipher_buf_pos = cipher_buf.begin() + read_length;
        plaintext_buf_pos = plaintext_buf.begin();
		char_counter = pos_rel_to_begin;
		block_type tmp2;
		reads(tmp2.data(), tmp2.data() + read_length);
        return ret;
	}
};

template <output_stream T, typename Enc>
class basic_octr
{
public:
    using native_interface_t = T;
    using char_type = typename native_interface_t::char_type;
    using cipher_type = Enc;

private:
    using unsigned_char_type = std::make_unsigned_t<char_type>;

public:
    using key_type = std::array<unsigned_char_type, cipher_type::key_size>;
    using block_type = std::array<unsigned_char_type, cipher_type::block_size>;
    using nonce_type = std::array<unsigned_char_type, cipher_type::block_size - sizeof(std::size_t)>;
private:
	using block_iterator = typename block_type::iterator;
    block_type plaintext_buf = {};
    block_iterator plaintext_buf_pos = plaintext_buf.begin();
    key_type key;
    nonce_type nonce;
    std::size_t char_counter;
    T ob;
    Enc enc;
    template<typename V>
    auto encrypt_out(V& v, std::size_t block_counter)
    {
        block_type block;
        memcpy(block.data(), nonce.data(), cipher_type::block_size - sizeof(std::size_t));
        // TODO: big-endian or small-endian
        memcpy(block.data() + cipher_type::block_size - sizeof(std::size_t), std::addressof(block_counter), sizeof(std::size_t));
        auto cipher(enc(block.data()));
        for (std::size_t i(0); i != cipher.size(); ++i)
            cipher[i] ^= v[i];
        ob.writes(cipher.cbegin(), cipher.cend());
        return block_counter;
    }

	void write_remain()
	{
        if (plaintext_buf_pos != plaintext_buf.begin())
        {
            std::uninitialized_fill(plaintext_buf_pos, plaintext_buf.end(), 0);
            auto block_counter(encrypt_out(plaintext_buf, char_counter / cipher_type::block_size));
            plaintext_buf_pos = plaintext_buf.begin();
            ++block_counter;
            char_counter = block_counter * cipher_type::block_size;
        }

	}

public:
    template<typename T1, typename T2,typename ...Args>
	requires std::constructible_from<key_type, T1> && std::constructible_from<nonce_type, T2> && std::constructible_from<T, Args...>
    basic_octr(T1&& init_key, T2&& nonce, Args&& ...args) : key(std::forward<T1>(init_key)), nonce(std::forward<T2>(nonce)), char_counter(0), ob(std::forward<Args>(args)...), enc(key.data())
    {
    }

    void flush()
    {
		write_remain();
		ob.flush();
    }
    template<std::contiguous_iterator Iter>
    void writes(Iter b, Iter e)
    {
        writes_precondition<unsigned_char_type>(b, e);
        auto pb(static_cast<unsigned_char_type const *>(static_cast<void const *>(std::to_address(b))));
        auto pi(pb), pe(pb + (e - b) * sizeof(*b) / sizeof(unsigned_char_type));
        std::size_t const input_length(pe - pi);

        if (plaintext_buf_pos != plaintext_buf.begin())
        {
            std::size_t min_length(plaintext_buf.end() - plaintext_buf_pos);
            if (input_length < min_length)
                min_length = input_length;
            plaintext_buf_pos = std::uninitialized_copy(pi, pi + min_length, plaintext_buf_pos);
            pi += min_length;
            char_counter += min_length;

            if (plaintext_buf_pos != plaintext_buf.end())
                return;

            encrypt_out(plaintext_buf, char_counter / cipher_type::block_size - 1);

            plaintext_buf_pos = plaintext_buf.begin();
        }
        std::size_t block_counter(char_counter / cipher_type::block_size);
        for (auto const last_length(pe - cipher_type::block_size); pi <= last_length; pi += cipher_type::block_size, char_counter += cipher_type::block_size)
        {
            encrypt_out(pi, block_counter);
            ++block_counter;
        }

        plaintext_buf_pos = std::uninitialized_copy(pi, pe, plaintext_buf.begin());
        char_counter += pe - pi;
    }

    void put(char_type ch) {
        if (plaintext_buf_pos == plaintext_buf.end())
        {
            encrypt_out(plaintext_buf, char_counter / cipher_type::block_size - 1);
            plaintext_buf_pos = plaintext_buf.begin();
        }
        *plaintext_buf_pos = static_cast<unsigned_char_type>(ch);
        ++plaintext_buf_pos;
        ++char_counter;
    }
	basic_octr(basic_octr const&)=delete;
	basic_octr& operator=(basic_octr const&)=delete;
	basic_octr(basic_octr&& octr) noexcept:plaintext_buf(std::move(plaintext_buf)),
    plaintext_buf_pos(std::move(octr.plaintext_buf_pos)),key(std::move(octr.key)),
	nonce(std::move(octr.nonce)),char_counter(std::move(octr.char_counter)),
							ob(std::move(octr.ob)),enc(std::move(octr.enc))
	{
		octr.plaintext_buf_pos=plaintext_buf.begin();
	}
	basic_octr& operator=(basic_octr&& octr) noexcept
	{
		if(std::addressof(octr)!=this)
		{
			try
			{
				flush();
			}
			catch(...){}
			plaintext_buf=std::move(octr.plaintext_buf);
			plaintext_buf_pos=std::move(octr.plaintext_buf_pos);
			key=std::move(octr.key);
			nonce=std::move(octr.nonce);
			char_counter=std::move(octr.char_counter);
			ob=std::move(octr.ob);
			enc=std::move(octr.enc);
			octr.plaintext_buf_pos=plaintext_buf.begin();
		}
	}
    ~basic_octr()
    try
    {
        write_remain();
    }
    catch (...)
    {
    }
	void swap(basic_octr &other) noexcept
	{
		using std::swap;
		swap(plaintext_buf,other.plaintext_buf);
		swap(plaintext_buf_pos,other.plaintext_buf_pos);
		swap(key,other.key);
		swap(nonce,other.nonce);
		swap(char_counter,other.char_counter);
		swap(ob,other.ob);
		swap(enc,other.enc);
	}
};

template <output_stream T, typename Enc>
inline void swap(basic_octr<T,Enc>& a,basic_octr<T,Enc>& b) noexcept
{
	a.swap(b);
}

} // namespace fast_io::crypto
