#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <vector>
#include <map>
#include <boost/assert.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>

namespace bantam
{
    enum class order_book_side
    {
        bid, ask
    };

    struct order_book_change
    {
        order_book_side side;
        double price;
        double volume;
    };

    struct order_book
    {
        using price_type = double;
        using map_type = std::map<price_type, double>;

        order_book() = default;
        void clear()
        {
            asks.clear();
            bids.clear();
        }
        bool update(map_type& m, price_type price, double volume)
        {
            BOOST_VERIFY(volume > 0);
            auto it = m.find(price);
            if (it == m.end())
            {
                m.emplace(price, volume);
                return true;
            }
            else
            {
                bool changed = it->second != volume;
                it->second = volume;
                return changed;
            }
        }
        bool update_bid(price_type price, double volume)
        {return volume ? update(bids, price, volume) : remove_bid(price);}
        bool update_ask(price_type price, double volume)
        {return volume ? update(asks, price, volume) : remove_ask(price);}
        bool remove(map_type& m, price_type price)
        {
            auto it = m.find(price);
            if (it != m.end())
            {
                m.erase(it);
                return true;
            }
            return false;
        }
        bool remove_bid(price_type price)
        {return remove(bids, price);}
        bool remove_ask(price_type price)
        {return remove(asks, price);}

        void print(std::ostream& out = std::cout, size_t max_size = 20) const
        {
            std::ostringstream os;
            size_t skip_ask = asks.size() > max_size ? asks.size() - max_size : 0;
            for (auto it = asks.rbegin(); it != asks.rend(); ++it)
            {
                if (skip_ask)
                {
                    skip_ask--;
                    continue;
                }
                const auto& a = *it;
                os << std::fixed << std::setprecision(8) << std::setw(16) << a.first << " - " << std::setw(4) << a.second << std::endl;
            }
            os << "---" << std::endl;
            size_t num_bids = 0;
            for (auto it = bids.rbegin(); it != bids.rend(); ++it)
            {
                const auto& a = *it;
                os << std::fixed << std::setprecision(8) << std::setw(16) << a.first << " - " << std::setw(4) << a.second << std::endl;
                if (++num_bids == max_size)
                    break;
            }
            out << os.str() << std::flush;
        }
        double get_median_price() const
        {
            double median = 0;
            int num_sides = 0;
            if (asks.size())
            {
                median += asks.begin()->first;
                num_sides++;
            }
            if (bids.size())
            {
                median += bids.rbegin()->first;
                num_sides++;
            }
            if (num_sides)
                median /= num_sides;
            return median;
        }
        double get_min_ask() const
        {
            if (asks.empty())
                return std::numeric_limits<double>::max();
            return asks.begin()->first;
        }
        double get_min_ask_vol() const
        {
            if (asks.empty())
                return 0;
            return asks.begin()->second;
        }
        double get_max_bid() const
        {
            if (bids.empty())
                return std::numeric_limits<double>::min();
            return bids.rbegin()->first;
        }
        double get_max_bid_vol() const
        {
            if (bids.empty())
                return 0;
            return bids.rbegin()->second;
        }
        void buy_partial(double max_price, double volume, std::vector<order_book_change>& changes)
        {
            for (auto it = bids.rbegin(); it != bids.rend() && volume > 0;)
            {
                double& v = it->second;
                double p = it->first;
                if (p >= max_price)
                {
                    if (v > volume)
                    {
                        v -= volume;
                        volume = 0;
                    }
                    else
                    {
                        BOOST_ASSERT(volume > 0);
                        volume -= v;
                        v = 0;
                    }
                    changes.push_back(order_book_change{order_book_side::bid, p, v});
                    if (v == 0)
                    {
                        bids.erase(std::next(it).base());
                    }
                    else ++it;
                }
                else break;
            }
            if (volume > 0)
            {
                auto it = asks.find(max_price);
                if (it == asks.end())
                    it = asks.emplace(max_price, volume).first;
                else it->second += volume;
                changes.push_back(order_book_change{order_book_side::ask, it->first, it->second});
            }
        }
        void sell_partial(double min_price, double volume, std::vector<order_book_change>& changes)
        {
            for (auto it = asks.begin(); it != asks.end() && volume > 0;)
            {
                double& v = it->second;
                double p = it->first;
                if (p <= min_price)
                {
                    if (v > volume)
                    {
                        v -= volume;
                        volume = 0;
                    }
                    else
                    {
                        BOOST_ASSERT(volume > 0);
                        volume -= v;
                        v = 0;
                    }
                    changes.push_back(order_book_change{order_book_side::ask, p, v});
                    if (v == 0)
                        it = asks.erase(it);
                    else ++it;
                }
                else break;
            }
            if (volume > 0)
            {
                auto it = bids.find(min_price);
                if (it == bids.end())
                    it = bids.emplace(min_price, volume).first;
                else it->second += volume;
                changes.push_back(order_book_change{order_book_side::bid, it->first, it->second});
            }
        }
        std::vector<order_book_change> snapshot() const
        {
            std::vector<order_book_change> res;
            res.reserve(bids.size() + asks.size());
            for (auto& v : asks)
                res.push_back(order_book_change{order_book_side::ask, v.first, v.second});
            for (auto& v : bids)
                res.push_back(order_book_change{order_book_side::bid, v.first, v.second});
            return res;
        }
    private:
        std::map<price_type, double> bids, asks;
    };


}
#endif // ORDER_BOOK_H
