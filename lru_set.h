#ifndef U_LRU_HDR
#define U_LRU_HDR
#include "u_map.h"

namespace u {

template <typename K>
struct lru {
    lru(size_t max = 128);
    ~lru();

    void insert(const K &data);

    const K &operator[](const K &key) const;
    K &operator[](const K &key);

    bool has(const K &key) const;

    size_t size() const;

    void evict(size_t max);
    void evict();

protected:
    struct node {
        K data;
        node *prev;
        node *next;
        node(const K &data);
    };

    node *find(const K &key);
    void move_front(node *n);
    void remove(node *n);
    void insert_front(node *n);
    void remove_back();

private:
    node *m_head;
    node *m_tail;
    map<K, node*> m_map;
    size_t m_size;
    size_t m_max;
};

template <typename K>
inline lru<K>::node::node(const K &data)
    : data(data)
    , prev(nullptr)
    , next(nullptr)
{
}

template <typename K>
inline lru<K>::lru(size_t max)
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_size(0)
    , m_max(max)
{
}

template <typename K>
inline lru<K>::~lru() {
    for (node *current = m_head; current; ) {
        node *temp = current;
        current = current->next;
        delete temp;
    }
    m_size = 0;
}

template <typename K>
inline typename lru<K>::node *lru<K>::find(const K &key) {
    const auto it = m_map.find(key);
    return it != m_map.end() ? it->second : nullptr;
}

template <typename K>
inline void lru<K>::move_front(node *n) {
    if (n == m_head)
        return;
    remove(n);
    insert_front(n);
}

template <typename K>
inline void lru<K>::remove(node *n) {
    if (n->prev)
        n->prev->next = n->next;
    else
        m_head = n->next;

    if (n->next)
        n->next->prev = n->prev;
    else
        m_tail = n->prev;

    m_size--;
}

template <typename K>
inline void lru<K>::insert_front(node *n) {
    if (m_head) {
        n->next = m_head;
        m_head->prev = n;
        n->prev = nullptr;
        m_head = n;
    } else {
        m_head = n;
        m_tail = n;
    }
    m_size++;
}

template <typename K>
inline void lru<K>::remove_back() {
    assert(m_tail);
    node *temp = m_tail;
    m_tail = m_tail->prev;
    if (m_tail) {
        m_tail->next = nullptr;
    } else {
        m_head = nullptr;
        m_tail = nullptr;
    }
    m_map.erase(m_map.find(temp->data));
    delete temp;
    m_size--;
}

template <typename K>
inline void lru<K>::insert(const K &data) {
    const auto n = find(data);
    if (n) {
        n->data = data;
        move_front(n);
    } else {
        if (m_size >= m_max)
            remove_back();

        node *next = new node(data);
        insert_front(next);
        m_map.insert(u::make_pair(data, next));
    }
}

template <typename K>
inline bool lru<K>::has(const K &key) const {
    return m_map.find(key) != m_map.end();
}

template <typename K>
inline const K &lru<K>::operator[](const K &key) const {
    const auto n = m_map.find(key);
    move_front(n->second);
    return n->second->data;
}

template <typename K>
inline K &lru<K>::operator[](const K &key) {
    const auto n = m_map.find(key);
    move_front(n->second);
    return n->second->data;
}

template <typename K>
inline size_t lru<K>::size() const {
    return m_size;
}

template <typename K>
inline void lru<K>::evict(size_t max) {
    if (max < m_size)
        while (m_size != max)
            remove_back();
}

}

#endif
