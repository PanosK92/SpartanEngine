/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES =====================
 #include <atomic>
 #include <cassert>
 #include <memory>
 #include <mutex>
 #include <type_traits>
 #include <unordered_map>
 #include "RefCountTracker.h"
//================================

// Define SPARTAN_ASSERT for use in this header
#ifndef SPARTAN_ASSERT
    #ifndef NDEBUG
        #define SPARTAN_ASSERT(condition, message) assert((condition) && (message))
    #else
        #define SPARTAN_ASSERT(condition, message) ((void)0)
    #endif
#endif

//================================

namespace spartan
{

    /**
     * @brief Base class for objects that can be reference-counted.
     *
     * Provides thread-safe reference counting capabilities that can be used by smart pointers.
     * Objects inheriting from this class can be managed by Ref<T>.
     */
    class RefCounted
    {
    public:
    	/**
    	 * @brief Default constructor initializes reference count to 0.
    	 */
    	RefCounted()
    	{
    	    SPARTAN_TRACK_REFCOUNT_CREATE(this, RefCounted);
    	}
    
    	/**
    	 * @brief Copy constructor maintains the reference count at 0.
    	 *
    	 * When an object is copied, the new instance starts with a fresh reference count.
    	 */
    	RefCounted(const RefCounted&) noexcept
    	{
    	    SPARTAN_TRACK_REFCOUNT_CREATE(this, RefCounted);
    	}
    
    	/**
    	 * @brief Copy assignment operator doesn't affect reference count.
    	 *
    	 * Reference count is associated with object identity, not with its contents.
    	 */
    	RefCounted& operator=(const RefCounted&) noexcept { return *this; }
    
    	/**
    	 * @brief Move constructor maintains the reference count at 0.
    	 */
    	RefCounted(RefCounted&&) noexcept
    	{
    	    SPARTAN_TRACK_REFCOUNT_CREATE(this, RefCounted);
    	}
    
    	/**
    	 * @brief Move assignment operator doesn't affect reference count.
    	 */
    	RefCounted& operator=(RefCounted&&) noexcept { return *this; }
    
    	/**
    	 * @brief Virtual destructor for proper polymorphic behavior.
    	 */
    	virtual ~RefCounted()
    	{
    	    SPARTAN_TRACK_REFCOUNT_DESTROY(this);
    	}
    
    	/**
    	 * @brief Increments the reference count.
    	 * @return The new reference count.
    	 */
    	uint32_t IncRefCount() const noexcept
    	{
    	    uint32_t newCount = ++m_RefCount;
    	    SPARTAN_TRACK_REFCOUNT_INCREMENT(this, newCount);
    	    return newCount;
    	}
    
    	/**
    	 * @brief Decrements the reference count.
    	 * @return The new reference count.
    	 */
    	uint32_t DecRefCount() const noexcept
    	{
    		SPARTAN_ASSERT(m_RefCount > 0, "Reference count is already 0");
    		uint32_t newCount = --m_RefCount;
    		SPARTAN_TRACK_REFCOUNT_DECREMENT(this, newCount);
    		return newCount;
    	}
    
    	/**
    	 * @brief Gets the current reference count.
    	 * @return The current reference count.
    	 */
    	uint32_t GetRefCount() const noexcept { return m_RefCount; }
    
    private:
    	mutable std::atomic<uint32_t> m_RefCount{0};    // Using mutable to allow const objects to be reference counted
    };

    // -------------------------------------------------------

    /**
     * @brief Alias template for a unique pointer to type T.
     * @tparam T The type to manage.
     */
    template <typename T>
    using Scope = std::unique_ptr<T>;
    
    /**
     * @brief Creates a unique pointer to an object of type T.
     *
     * Creates and returns a unique pointer that has exclusive ownership
     * of the newly created object.
     *
     * @tparam T The type to manage.
     * @tparam Args The types of the arguments to pass to the constructor of T.
     * @param args The arguments to pass to the constructor of T.
     * @return A unique pointer to an object of type T.
     */
    template <typename T, typename... Args>
    constexpr Scope<T> CreateScope(Args &&...args)
    {
    	return std::make_unique<T>(std::forward<Args>(args)...);
    }

    // ----------------------------------------------------------

    namespace Internal
    {
        /**
         * @brief Bridge class for enabling std::shared_ptr interoperability with Ref<T>
         *
         * The SharedPtrBridge acts as an intermediary that allows Ref<T> and std::shared_ptr<T>
         * to share ownership of the same object. When a Ref is created from a shared_ptr or
         * vice versa, this bridge ensures that:
         * 1. Both reference counting systems are aware of each other
         * 2. The object is only destroyed when both systems agree (all references are gone)
         * 3. No double-deletion occurs
         *
         * The bridge maintains:
         * - A weak_ptr to the std::shared_ptr's control block
         * - A pointer to the object itself
         * - Integration with the ControlBlockRegistry for WeakRef support
         *
         * @tparam T The type of object being shared between the two systems
         */
        template <typename T>
        class SharedPtrBridge
        {
        public:
            /**
             * @brief Constructs a bridge from an existing std::shared_ptr
             *
             * @param shared The std::shared_ptr to bridge with
             */
            explicit SharedPtrBridge(const std::shared_ptr<T>& shared) noexcept : m_WeakPtr(shared), m_RawPtr(shared.get()) {}
        
            /**
             * @brief Gets the raw pointer to the object
             *
             * @return Pointer to the managed object, or nullptr if expired
             */
            T* GetPtr() const noexcept 
            { 
                return m_WeakPtr.expired() ? nullptr : m_RawPtr; 
            }
        
            /**
             * @brief Checks if the std::shared_ptr side has expired
             *
             * @return true if the shared_ptr has been destroyed
             */
            [[nodiscard]] bool IsExpired() const noexcept 
            { 
                return m_WeakPtr.expired(); 
            }
        
            /**
             * @brief Attempts to lock and get a std::shared_ptr
             *
             * @return A std::shared_ptr if still valid, or empty if expired
             */
            std::shared_ptr<T> Lock() const noexcept 
            { 
                return m_WeakPtr.lock(); 
            }
        
        private:
            std::weak_ptr<T> m_WeakPtr;  // Weak reference to the shared_ptr's control block
            T* m_RawPtr;                 // Raw pointer for quick access (checked via m_WeakPtr)
        };
        
        /**
         * @brief Control block for managing weak references to an object
         *
         * The ControlBlock is responsible for tracking weak references to an object
         * even after the object itself has been destroyed. It maintains:
         * 1. A pointer to the actual object (which becomes nullptr when the object is destroyed)
         * 2. A count of weak references pointing to this control block
         *
         * When an object is destroyed but weak references to it still exist, the control
         * block remains alive (with m_Ptr set to nullptr) until all weak references are gone.
         *
         * @tparam T The type of object being referenced
         */
        template <typename T>
        class ControlBlock
        {
        public:
            /**
             * @brief Constructs a control block for the specified object
             *
             * @param ptr Pointer to the object being tracked
             */
            explicit ControlBlock(T *ptr) noexcept : m_Ptr(ptr), m_WeakCount(0) {}
            
            /**
             * @brief Increments the weak reference count
             *
             * Called when a new WeakRef is created or copied to point to this object
             */
            void IncWeakCount() noexcept { ++m_WeakCount; }
            
            /**
             * @brief Decrements the weak reference count
             *
             * When the weak count reaches zero and the object pointer is nullptr
             * (indicating the object has been destroyed), the control block
             * deletes itself as it's no longer needed.
             */
            void DecWeakCount() noexcept
            {
                if (--m_WeakCount == 0 && m_Ptr == nullptr)
                {
                    delete this;
                }
            }
            
            /**
             * @brief Gets the pointer to the managed object
             *
             * @return The pointer to the object, or nullptr if the object has been destroyed
             */
            T *GetPtr() const noexcept { return m_Ptr; }
            
            /**
             * @brief Sets the object pointer
             *
             * This is typically called with nullptr when the object is being destroyed
             * to indicate that the object is no longer valid.
             *
             * @param ptr The new object pointer value
             */
            void SetPtr(T *ptr) noexcept
            {
                m_Ptr = ptr;
            }
            
            /**
             * @brief Gets the current weak reference count
             *
             * @return The number of weak references pointing to this control block
             */
            uint32_t GetWeakCount() const noexcept
            {
                return m_WeakCount;
            }
        
        private:
            T *m_Ptr;                          // Pointer to the managed object, or nullptr if destroyed
            std::atomic<uint32_t> m_WeakCount; // Number of weak references to this object
        };
        
        /**
         * @brief Registry for managing control blocks for weak references
         *
         * This class maintains a mapping from object pointers to their associated control blocks,
         * allowing weak references to locate the control block for an object they're referencing.
         * It ensures that multiple weak references to the same object share the same control block.
         *
         * The registry is implemented as a singleton to provide global access while ensuring
         * there's only one instance managing all control blocks for a given type T.
         *
         * @tparam T The type of objects being tracked in this registry
         */
        template <typename T>
        class ControlBlockRegistry
        {
        public:
            /**
             * @brief Get the singleton instance of the registry
             *
             * @return A reference to the singleton instance
             */
            static ControlBlockRegistry &GetInstance()
            {
                static ControlBlockRegistry instance;
                return instance;
            }
        
            /**
             * @brief Get or create a control block for the specified object pointer
             *
             * If a control block already exists for the given pointer, it returns that block.
             * Otherwise, it creates a new control block, registers it, and returns it.
             *
             * @param ptr Pointer to the object for which to get/create a control block
             * @return Pointer to the control block, or nullptr if ptr is nullptr
             */
            ControlBlock<T> *GetControlBlock(T *ptr)
            {
                if (!ptr)
                {
                    return nullptr;
                }
        
                std::scoped_lock lock(m_Mutex);
                auto it = m_Blocks.find(ptr);
                if (it != m_Blocks.end())
                {
                    return it->second;
                }
        
                auto block = new Internal::ControlBlock<T>(ptr);
                m_Blocks[ptr] = block;
                return block;
            }
        
            /**
             * @brief Remove the control block associated with the specified object pointer
             *
             * This method is called when an object is being destroyed. It sets the object pointer
             * in the control block to nullptr to indicate that the object is no longer valid.
             * If there are no weak references to the object, the control block itself is deleted.
             *
             * @param ptr Pointer to the object whose control block should be removed
             */
            void RemoveControlBlock(T *ptr)
            {
                if (!ptr)
                {
                    return;
                }
        
                std::scoped_lock lock(m_Mutex);
                auto it = m_Blocks.find(ptr);
                if (it != m_Blocks.end())
                {
                    it->second->SetPtr(nullptr);
                    if (it->second->GetWeakCount() == 0)
                    {
                        delete it->second;
                    }

                    m_Blocks.erase(it);
                }
            }
        
        private:
            /**
             * @brief Private constructor to enforce singleton pattern
             */
            ControlBlockRegistry() = default;
            ~ControlBlockRegistry()
            {
                for (auto &pair : m_Blocks)
                {
                    delete pair.second;
                }
            }
        
            std::unordered_map<T *, Internal::ControlBlock<T> *> m_Blocks;
            std::mutex m_Mutex;
        };
    } // namespace Internal

    // -----------------------------------------------------------

    template <typename T>
    class WeakRef;

    // ---------------------------------------------------------

    /**
     * @brief A reference-counting smart pointer that manages shared ownership of objects.
     *
     * The Ref class provides a reference-counting ownership mechanism where multiple
     * Ref instances can share ownership of a single object. The object is destroyed
     * when the last Ref pointing to it is destroyed or reset.
     *
     * @tparam T The type of the managed object.
     */
    template <typename T>
    class Ref
    {
    public:
    	/**
    	 * @brief Default constructor creates a null reference.
    	 */
    	constexpr Ref() noexcept = default;
    
    	/**
    	 * @brief Constructor from nullptr creates a null reference.
    	 */
    	constexpr Ref(std::nullptr_t) noexcept {}
    
    	/**
    	 * @brief Constructor from raw pointer. Takes ownership of the object.
    	 *
    	 * This constructor increments the reference count of the object.
    	 *
    	 * @param ptr Pointer to the object to manage.
    	 */
    	template <typename U>
    	explicit Ref(U* ptr) noexcept requires (std::is_convertible_v<U*,T*>) : m_Ptr(ptr)
    	{
    	    InternalAddRef();
    	}
    
    	/**
    	 * @brief Copy constructor. Shares ownership of the object.
    	 *
    	 * This constructor increments the reference count of the object.
    	 *
    	 * @param other The Ref to copy from.
    	 */
    	Ref(const Ref& other) noexcept : m_Ptr(other.m_Ptr)
    	{
    	    InternalAddRef();
    	}
    
    	/**
    	 * @brief Copy constructor with type conversion. Shares ownership of the object.
    	 *
    	 * This constructor increments the reference count of the object and allows
    	 * converting between compatible types.
    	 *
    	 * @tparam U The type of the other Ref.
    	 * @param other The Ref to copy from.
    	 */
    	template <typename U>
    	Ref(const Ref<U>& other) noexcept requires (std::is_convertible_v<U*,T*>) : m_Ptr(other.Get())
    	{
    	    InternalAddRef();
    	}
    
    	/**
    	 * @brief Move constructor. Takes ownership from another Ref.
    	 * This constructor doesn't change the reference count of the object.
    	 * @param other The Ref to move from.
    	 */
    	Ref(Ref&& other) noexcept : m_Ptr(other.m_Ptr) { other.m_Ptr = nullptr; }
    
    	/**
    	 * @brief Move constructor with type conversion. Takes ownership from another Ref.
    	 * This constructor doesn't change the reference count of the object and allows
    	 * converting between compatible types.
    	 * @tparam U The type of the other Ref.
    	 * @param other The Ref to move from.
    	 */
    	template <typename U>
    	Ref(Ref<U>&& other) noexcept requires(std::is_convertible_v<U*, T*>) : m_Ptr(other.Get())
    	{
    	    other.m_Ptr = nullptr;
    	}
    
    	/**
    	 * @brief Constructor from std::shared_ptr. Shares ownership of the object.
    	 *
    	 * This constructor allows interoperability with std::shared_ptr.
    	 *
    	 * @param shared The std::shared_ptr to convert from.
    	 */
    	explicit Ref(const std::shared_ptr<T>& shared) noexcept : m_Ptr(shared.get())
    	{
    	    InternalAddRef();
    	}
    
    	/**
    	 * @brief Constructor from std::shared_ptr with type conversion.
    	 *
    	 * This constructor allows creation of a Ref<T> from a std::shared_ptr<U>
    	 * where U is convertible to T. This enables interoperability with standard
    	 * library smart pointers while maintaining type safety.
    	 *
    	 * @tparam U Source type that is convertible to T
    	 * @param shared The std::shared_ptr<U> to convert from
    	 * @note The object must inherit from RefCounted for proper reference counting
    	 */
    	template <typename U>
    	explicit Ref(const std::shared_ptr<U>& shared) noexcept requires (std::is_convertible_v<U*,T*>) : m_Ptr(static_cast<T*>(shared.get()))
    	{
    	    InternalAddRef();
    	}
    
    	/**
    	 * @brief Constructor from WeakRef. Obtains a strong reference if available.

    	 * This constructor attempts to obtain a strong reference from a WeakRef.
    	 * If the WeakRef has expired, the Ref will be null.
    	 *
    	 * @param weak The WeakRef to convert from.
    	 */
    	explicit Ref(const WeakRef<T>& weak) noexcept;
    
    	/**
    	 * @brief Destructor. Decrements the reference count of the object.
    	 *
    	 * If the reference count reaches 0, the object is destroyed.
    	 */
    	~Ref()
    	{
    	    InternalRelease();
    	}
    
    	/**
    	 * @brief Copy assignment operator. Shares ownership of the object.
    	 *
    	 * This operator increments the reference count of the assigned object
    	 * and decrements the reference count of the previously managed object.
    	 *
    	 * @param other The Ref to copy from.
    	 * @return Reference to this Ref.
    	 */
    	Ref& operator=(const Ref& other) noexcept
    	{
    	    if (this != &other)
    	    {
    	    	InternalRelease();
    	    	m_Ptr = other.m_Ptr;
    	    	InternalAddRef();
    	    }
    	    return *this;
    	}
    
    	/**
    	 * @brief Copy assignment operator with type conversion. Shares ownership of the object.
    	 *
    	 * This operator increments the reference count of the assigned object,
    	 * decrements the reference count of the previously managed object, and allows
    	 * converting between compatible types.
    	 *
    	 * @tparam U The type of the other Ref.
    	 * @param other The Ref to copy from.
    	 * @return Reference to this Ref.
    	 */
    	template <typename U>
    	Ref& operator=(const Ref<U>& other) noexcept requires (std::is_convertible_v<U*,T*>)
        {
    	    /// Self-assignment or assigning same pointer: do nothing
    	    if (static_cast<const void*>(this) == static_cast<const void*>(&other) || m_Ptr == other.Get())
    	        return *this;
    	    InternalRelease();
    	    m_Ptr = other.Get();
    	    InternalAddRef();
    	    return *this;
    	}
    
    	/**
    	 * @brief Move assignment operator. Takes ownership from another Ref.
    	 *
    	 * This operator decrements the reference count of the previously managed object
    	 * and takes ownership of the object from the other Ref without changing its
    	 * reference count.
    	 *
    	 * @param other The Ref to move from.
    	 * @return Reference to this Ref.
    	 */
    	Ref& operator=(Ref&& other) noexcept
    	{
    	    if (this != &other)
    	    {
    	    	InternalRelease();
    	    	m_Ptr = other.m_Ptr;
    	    	other.m_Ptr = nullptr;
    	    }
    	    return *this;
    	}
    
    	/**
    	 * @brief Move assignment operator with type conversion. Takes ownership from another Ref.
    	 *
    	 * This operator decrements the reference count of the previously managed object,
    	 * takes ownership of the object from the other Ref without changing its reference count,
    	 * and allows converting between compatible types.
    	 *
    	 * @tparam U The type of the other Ref.
    	 * @param other The Ref to move from.
    	 * @return Reference to this Ref.
    	 */
    	template <typename U>
    	Ref& operator=(Ref<U>&& other) noexcept requires (std::is_convertible_v<U*,T*>)
        {
    	    // Self-move or moving same pointer: do nothing
    	    if (static_cast<void*>(this) == static_cast<void*>(&other) || m_Ptr == other.Get())
    	        return *this;
    	    InternalRelease();
    	    m_Ptr = other.Get();
    	    other.m_Ptr = nullptr;
    	    return *this;
    	}
    
    	/**
    	 * @brief Assignment operator from nullptr. Resets the Ref.
    	 *
    	 * This operator decrements the reference count of the previously managed object
    	 * and sets the Ref to null.
    	 *
    	 * @return Reference to this Ref.
    	 */
    	Ref& operator=(std::nullptr_t) noexcept
    	{
    	    InternalRelease();
    	    return *this;
    	}
    
    	/**
    	 * @brief Assignment operator from std::shared_ptr.
    	 *
    	 * This operator allows assigning a std::shared_ptr to a Ref, enabling
    	 * interoperability with standard library smart pointers. The Ref will
    	 * share ownership with the shared_ptr.
    	 *
    	 * @param shared The std::shared_ptr to assign from
    	 * @return Reference to this Ref
    	 * @note The object must inherit from RefCounted for proper reference counting
    	 */
    	Ref& operator=(const std::shared_ptr<T>& shared) noexcept
    	{
    	    InternalRelease();
    	    m_Ptr = shared.get();
    	    InternalAddRef();
    	    return *this;
    	}
    
    	/**
    	 * @brief Assignment operator from std::shared_ptr with type conversion.
    	 *
    	 * This operator allows assigning a std::shared_ptr<U> to a Ref<T> where
    	 * U is convertible to T, enabling interoperability with standard library
    	 * smart pointers while maintaining type safety.
    	 *
    	 * @tparam U Source type that is convertible to T
    	 * @param shared The std::shared_ptr<U> to assign from
    	 * @return Reference to this Ref
    	 * @note The object must inherit from RefCounted for proper reference counting
    	 */
    	template <typename U>
    	Ref& operator=(const std::shared_ptr<U>& shared) noexcept requires (std::is_convertible_v<U*,T*>)
    	{
    	    InternalRelease();
    	    m_Ptr = static_cast<T*>(shared.get());
    	    InternalAddRef();
    	    return *this;
    	}
    
    	/**
    	 * @brief Dereference operator. Provides access to the managed object.
    	 *
    	 * @return Reference to the managed object.
    	 */
    	T& operator*() const noexcept
    	{
    	    SPARTAN_ASSERT(m_Ptr, "Dereferencing null Ref");
    	    return *m_Ptr;
    	}
    
    	/**
    	 * @brief Arrow operator. Provides access to the managed object's members.
    	 *
    	 * @return Pointer to the managed object.
    	 */
    	T* operator->() const noexcept
    	{
    	    SPARTAN_ASSERT(m_Ptr, "Accessing member of null Ref");
    	    return m_Ptr;
    	}
    
    	/**
    	 * @brief Boolean conversion operator. Checks if the Ref is not null.
    	 *
    	 * @return True if the Ref is not null, false otherwise.
    	 */
    	explicit operator bool() const noexcept
    	{
    	    return m_Ptr != nullptr;
    	}
    
    	/**
    	 * @brief Gets the raw pointer to the managed object.
    	 *
    	 * @return Pointer to the managed object.
    	 */
    	T* Get() const noexcept
    	{
    	    return m_Ptr;
    	}
    
    	/**
    	 * @brief Resets the Ref to null or to manage a new object.
    	 *
    	 * This method decrements the reference count of the previously managed object
    	 * and sets the Ref to manage a new object or to null if no object is provided.
    	 *
    	 * @param ptr Pointer to the new object to manage, or nullptr.
    	 */
    	void Reset(T* ptr = nullptr) noexcept
    	{
    	    InternalRelease();
    	    m_Ptr = ptr;
    	    InternalAddRef();
    	}
    
    	/**
    	 * @brief Checks if this Ref is the only one managing the object.
    	 *
    	 * @return True if the reference count is 1, false otherwise or if null.
    	 */
    	bool IsUnique() const noexcept
    	{
    	    return m_Ptr && m_Ptr->GetRefCount() == 1;
    	}
    
    	/**
    	 * @brief Converts this Ref to a Ref of another type using static_cast.
    	 *
    	 * @tparam U The type to convert to.
    	 * @return A Ref<U> managing the same object.
    	 */
    	template <typename U>
    	Ref<U> As() const noexcept
    	{
    	    return Ref<U>(static_cast<U*>(m_Ptr));
    	}
    
    	/**
    	 * @brief Converts this Ref to a Ref of another type using dynamic_cast.
    	 *
    	 * @tparam U The type to convert to.
    	 * @return A Ref<U> managing the same object, or null if the cast fails.
    	 */
    	template <typename U>
    	Ref<U> DynamicCast() const noexcept
    	{
    	    if (U* cast = dynamic_cast<U*>(m_Ptr))
    	    {
    	        return Ref<U>(cast);
    	    }

    	    return Ref<U>();
    	}
    
    	/**
         * @brief Converts this Ref to a std::shared_ptr.
         *
         * This method creates a std::shared_ptr from this Ref with a custom deleter
         * that decrements the reference count. This allows interoperability with
         * functions that expect std::shared_ptr.
         *
         * @return A std::shared_ptr managing the same object.
         */
        [[nodiscard]] std::shared_ptr<T> ToSharedPtr() const noexcept
    	{
    	    if (!m_Ptr)
    	    {
    	        return nullptr;
    	    }
    
    	    // Increment the ref count for the shared_ptr
    	    InternalAddRef();
    
    	    // Create a shared_ptr with a custom deleter that decrements the ref count
    	    return std::shared_ptr<T>(m_Ptr, [](T* ptr)
    	    {
    	    	if (ptr && ptr->DecRefCount() == 0)
    	    	{
    	    	    delete ptr;
    	    	}
    	    });
    	}
    
    	/**
    	 * @brief Swaps the contents of this Ref with another.
    	 *
    	 * @param other The Ref to swap with.
    	 */
    	void Swap(Ref& other) noexcept
    	{
    	    std::swap(m_Ptr, other.m_Ptr);
    	}
    
    	/**
         * @brief Checks if the Ref is not null.
         *
         * @return True if the Ref is not null, false otherwise.
         */
        [[nodiscard]] bool IsValid() const noexcept
    	{
    	    return m_Ptr != nullptr;
    	}
    
    	/**
         * @brief Gets the reference count of the managed object.
         *
         * @return The reference count, or 0 if the Ref is null.
         */
        [[nodiscard]] uint32_t UseCount() const noexcept
    	{
    	    return m_Ptr ? m_Ptr->GetRefCount() : 0;
    	}
    
    	/**
    	 * @brief Equality operator. Compares the managed objects.
    	 *
    	 * @param other The Ref to compare with.
    	 * @return True if both Refs manage the same object, false otherwise.
    	 */
    	bool operator==(const Ref& other) const noexcept { return m_Ptr == other.m_Ptr; }
    
    	/**
    	 * @brief Inequality operator. Compares the managed objects.
    	 *
    	 * @param other The Ref to compare with.
    	 * @return True if the Refs manage different objects, false otherwise.
    	 */
    	bool operator!=(const Ref& other) const noexcept { return m_Ptr != other.m_Ptr; }
    
    	/**
    	 * @brief Equality operator with nullptr. Checks if the Ref is null.
    	 *
    	 * @return True if the Ref is null, false otherwise.
    	 */
    	bool operator==(std::nullptr_t) const noexcept { return m_Ptr == nullptr; }
    
    	/**
    	 * @brief Inequality operator with nullptr. Checks if the Ref is not null.
    	 *
    	 * @return True if the Ref is not null, false otherwise.
    	 */
    	bool operator!=(std::nullptr_t) const noexcept { return m_Ptr != nullptr; }
    
    private:
    	// Helper for SFINAE-based object comparison
    	template <typename U>
    	static auto HasEqualityOperator(int) -> decltype(std::declval<U>() == std::declval<U>(), std::true_type{});
    
    	template <typename U>
    	static std::false_type HasEqualityOperator(...);
    
    	template <typename U>
    	static bool CompareObjectsImpl(const U& a, const U& b, std::true_type)
    	{
    	    return a == b;
    	}
    
    	template <typename U>
    	static bool CompareObjectsImpl(const U& a, const U& b, std::false_type)
    	{
    	    return false; // No equality operator available
    	}
    
    public:
    	/**
         * @brief Compares the managed objects for object equality.
         *
         * This method compares the objects themselves, not just the pointers.
         * If `T` does not define `operator==`, the comparison returns false.
         *
         * @param other The Ref to compare with.
         * @return True if both objects are equal, false otherwise.
         */
        [[nodiscard]] bool EqualsObject(const Ref& other) const noexcept
    	{
    	    if (m_Ptr == other.m_Ptr)
    	    {
    	        return true;
    	    }
    
    	    if (!m_Ptr || !other.m_Ptr)
    	    {
    	        return false;
    	    }
    
    	    // Use compile-time type trait detection instead of runtime function calls
    	    typedef decltype(HasEqualityOperator<T>(0)) HasEquality;
    	    return CompareObjectsImpl(*m_Ptr, *other.m_Ptr, HasEquality{});
    	 }
    
    private:
    	T* m_Ptr = nullptr;
    
    	void InternalAddRef() const noexcept;
    	void InternalRelease() noexcept;
    
        // Grant access to specific classes or functions
        template <typename U>
        friend class Ref;
        
        template <typename U>
        friend class WeakRef;
    };

    // -------------------------------------------------------

    /**
     * @brief A weak reference to an object managed by Ref<T>.
     *
     * WeakRef allows observing an object without affecting its lifetime.
     * Unlike Ref<T>, WeakRef does not prevent the object from being destroyed.
     *
     * @tparam T The type of the managed object.
     */
    template <typename T>
    class WeakRef
    {
    public:
        /**
         * @brief Default constructor creates an empty weak reference.
         */
        constexpr WeakRef() noexcept = default;
    
        /**
         * @brief Constructor from nullptr creates an empty weak reference.
         */
        constexpr WeakRef(std::nullptr_t) noexcept {}
    
        /**
         * @brief Constructor from a Ref<T>. Creates a weak reference to the object managed by ref.
         *
         * @param ref The Ref<T> to observe.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        WeakRef(const Ref<U>& ref) noexcept;
    
        /**
         * @brief Copy constructor.
         *
         * @param other The WeakRef to copy from.
         */
        WeakRef(const WeakRef& other) noexcept;
    
        /**
         * @brief Copy constructor with type conversion.
         *
         * @tparam U The type of the other WeakRef.
         * @param other The WeakRef to copy from.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        WeakRef(const WeakRef<U>& other) noexcept;
    
        /**
         * @brief Move constructor.
         *
         * @param other The WeakRef to move from.
         */
        WeakRef(WeakRef&& other) noexcept;
    
        /**
         * @brief Move constructor with type conversion.
         *
         * @tparam U The type of the other WeakRef.
         * @param other The WeakRef to move from.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        WeakRef(WeakRef<U>&& other) noexcept;
    
        /**
         * @brief Destructor.
         */
        ~WeakRef();
    
        Internal::ControlBlock<T> *GetControlBlock() const noexcept
        {
            return m_ControlBlock;
        }
    
        /**
         * @brief Copy assignment operator.
         *
         * @param other The WeakRef to copy from.
         * @return Reference to this WeakRef.
         */
        WeakRef& operator=(const WeakRef& other) noexcept;
    
        /**
         * @brief Copy assignment operator with type conversion.
         *
         * @tparam U The type of the other WeakRef.
         * @param other The WeakRef to copy from.
         * @return Reference to this WeakRef.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        WeakRef& operator=(const WeakRef<U>& other) noexcept;
    
        /**
         * @brief Move assignment operator.
         *
         * @param other The WeakRef to move from.
         * @return Reference to this WeakRef.
         */
        WeakRef& operator=(WeakRef&& other) noexcept;
    
        /**
         * @brief Move assignment operator with type conversion.
         *
         * @tparam U The type of the other WeakRef.
         * @param other The WeakRef to move from.
         * @return Reference to this WeakRef.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        WeakRef& operator=(WeakRef<U>&& other) noexcept;
    
        /**
         * @brief Assignment operator from Ref<T>.
         *
         * @param ref The Ref<T> to observe.
         * @return Reference to this WeakRef.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        WeakRef& operator=(const Ref<U>& ref) noexcept;
    
        /**
         * @brief Constructor from std::weak_ptr.
         *
         * This constructor creates a WeakRef from a std::weak_ptr, enabling
         * interoperability with standard library smart pointers. The object
         * pointed to by the weak_ptr must inherit from RefCounted.
         *
         * The implementation:
         * 1. Attempts to lock the std::weak_ptr to get a temporary shared_ptr
         * 2. If successful, retrieves or creates a control block for the object
         * 3. Increments the weak reference count
         *
         * @param weak The std::weak_ptr to convert from
         * @note If the weak_ptr is expired, the WeakRef will be empty
         */
        explicit WeakRef(const std::weak_ptr<T>& weak) noexcept;
    
        /**
         * @brief Assignment operator from std::weak_ptr.
         *
         * This operator allows assigning a std::weak_ptr to a WeakRef, enabling
         * interoperability with standard library smart pointers.
         *
         * The implementation:
         * 1. Decrements the weak reference count of the current control block (if any)
         * 2. Attempts to lock the std::weak_ptr to get a temporary shared_ptr
         * 3. If successful, retrieves or creates a control block for the object
         * 4. Increments the weak reference count
         *
         * @param weak The std::weak_ptr to assign from
         * @return Reference to this WeakRef
         */
        WeakRef& operator=(const std::weak_ptr<T>& weak) noexcept;
    
        /**
         * @brief Assignment operator from nullptr.
         *
         * This operator allows assigning nullptr to a WeakRef, which effectively
         * resets the weak reference. It decrements the weak reference count in the
         * associated control block (if any) and sets the control block pointer to nullptr.
         *
         * The implementation:
         * 1. Decrements the weak reference count if a valid control block exists
         * 2. Sets the control block pointer to nullptr
         *
         * @param unused Nullptr value (not used in the implementation)
         * @return A reference to this WeakRef after the assignment
         */
        WeakRef& operator=(std::nullptr_t) noexcept;
    
        /**
         * @brief Checks if the WeakRef is expired.
         *
         * A WeakRef is expired if the object it points to has been destroyed.
         *
         * @return True if the WeakRef is expired, false otherwise.
         */
        bool Expired() const noexcept;
    
        /**
         * @brief Attempts to get a strong reference to the object.
         *
         * @return A Ref<T> to the object, or an empty Ref<T> if the object has been destroyed.
         */
        Ref<T> Lock() const noexcept;
    
        /**
         * @brief Resets the WeakRef.
         */
        void Reset() noexcept;
    
        /**
         * @brief Gets the reference count of the object.
         *
         * @return The number of Ref<T> instances that share ownership of the object, or 0 if the WeakRef is expired.
         */
        uint32_t UseCount() const noexcept;
    
        /**
         * @brief Equality operator.
         *
         * @param other The WeakRef to compare with.
         * @return True if both WeakRefs observe the same object, false otherwise.
         */
        bool operator==(const WeakRef& other) const noexcept;
    
        /**
         * @brief Inequality operator.
         *
         * @param other The WeakRef to compare with.
         * @return True if the WeakRefs observe different objects, false otherwise.
         */
        bool operator!=(const WeakRef& other) const noexcept;
    
    private:
        Internal::ControlBlock<T>* m_ControlBlock = nullptr;
    
        // Allow all WeakRef instantiations to access each other's private members
        template <typename> friend class WeakRef;
    
        // Allow Ref<T> to access m_ControlBlock
        template <typename U>
        friend class Ref;
    };

    // -------------------------------------------------------

    /**
     * @brief Creates a reference-counted object of type T.
     *
     * This function creates a new instance of T and wraps it in a Ref<T>.
     * The type T must inherit from RefCounted.
     *
     * @tparam T The type to create.
     * @tparam Args The types of the arguments to pass to the constructor of T.
     * @param args The arguments to pass to the constructor of T.
     * @return A Ref<T> managing the new object.
     */
    template <typename T, typename... Args>
    Ref<T> CreateRef(Args&&... args)
    {
    	static_assert(std::is_base_of_v<RefCounted, T>, "Type must inherit from RefCounted");
    return Ref<T>(new T(std::forward<Args>(args)...));
    }
    
    /**
     * @brief Creates a Ref<T> from a std::shared_ptr<T>.
     *
     * This function allows converting objects created with std::make_shared
     * to the spartan Ref system. The object must inherit from RefCounted.
     * Both the returned Ref and the original shared_ptr will share ownership.
     *
     * @tparam T The type of the object (must inherit from RefCounted)
     * @param shared The std::shared_ptr to convert from
     * @return A Ref<T> managing the same object
     * @note This maintains compatibility between std::shared_ptr and Ref counting systems
     */
    template <typename T>
    Ref<T> MakeRefFromShared(const std::shared_ptr<T>& shared)
    {
    	static_assert(std::is_base_of_v<RefCounted, T>, "Type must inherit from RefCounted");
    	return Ref<T>(shared);
    }
    
    /**
     * @brief Creates a Ref<T> from a std::shared_ptr<U> with type conversion.
     *
     * This function allows converting objects created with std::make_shared
     * to the spartan Ref system with type conversion. The object must inherit
     * from RefCounted and U must be convertible to T.
     *
     * @tparam T The target type
     * @tparam U The source type (must be convertible to T)
     * @param shared The std::shared_ptr<U> to convert from
     * @return A Ref<T> managing the same object
     */
    template <typename T, typename U>
    Ref<T> MakeRefFromShared(const std::shared_ptr<U>& shared)
    {
    	static_assert(std::is_base_of_v<RefCounted, T>, "Type must inherit from RefCounted");
    	static_assert(std::is_convertible_v<U*, T*>, "U* must be convertible to T*");
    	return Ref<T>(shared);
    }

    /**
     * @brief Creates a std::shared_ptr<T> from a Ref<T>.
     *
     * This function enables passing spartan Ref objects to APIs that expect
     * std::shared_ptr. The returned shared_ptr will share ownership with the
     * original Ref through a custom deleter.
     *
     * @tparam T The type of the object
     * @param ref The Ref<T> to convert from
     * @return A std::shared_ptr<T> managing the same object
     * @note This is equivalent to calling ref.ToSharedPtr()
     */
    template <typename T>
    std::shared_ptr<T> MakeSharedFromRef(const Ref<T>& ref)
    {
    	return ref.ToSharedPtr();
    }

    /**
     * @brief Increments the reference count for the object.
     *
     * This method is called when a new reference to the object is created.
     * It safely increments the internal reference counter of the pointed object
     * if the pointer is not null.
     *
     * @tparam T The type of the reference-counted object
     * @return void
     */
    template <typename T>
    void Ref<T>::InternalAddRef() const noexcept
    {
        if (m_Ptr)
        {
            m_Ptr->IncRefCount();
        }
    }

    /**
     * @brief Releases a reference to the object and potentially deletes it.
     *
     * This method decrements the reference count of the pointed object.
     * If the reference count reaches zero, it updates any weak references
     * through the ControlBlockRegistry to indicate that the object is no longer valid,
     * and then deletes the object.
     *
     * The method ensures that:
     * 1. Weak references can detect that the object has been destroyed
     * 2. The object memory is properly freed when no more strong references exist
     * 3. The internal pointer is set to nullptr after the release
     *
     * @tparam T The type of the reference-counted object
     * @return void
     */
    template <typename T>
    void Ref<T>::InternalRelease() noexcept
    {
        if (m_Ptr)
        {
            if (m_Ptr->DecRefCount() == 0)
            {
                // Update any weak references before deleting the object
                Internal::ControlBlockRegistry<T>::GetInstance().RemoveControlBlock(m_Ptr);
                delete m_Ptr;
            }
            m_Ptr = nullptr;
        }
    }

    /**
     * @brief Constructs a weak reference from a strong reference.
     *
     * This constructor creates a WeakRef that weakly references the same object
     * as the provided strong reference (Ref<U>). The constructor supports proper
     * type conversion through the template parameter U, which must be convertible to T.
     *
     * The implementation:
     * 1. Checks if the provided reference is valid
     * 2. Retrieves or creates a control block for the referenced object
     * 3. Increments the weak reference count in the control block
     *
     * @tparam U The type of the source reference, must be convertible to T
     * @param ref The strong reference to create a weak reference from
     * @note - This constructor enables implicit conversion from Ref<U> to WeakRef<T>
     *       when U is convertible to T
     */
    template <typename T>
    template <typename U, typename>
    WeakRef<T>::WeakRef(const Ref<U> &ref) noexcept
    {
        if (ref)
        {
            m_ControlBlock =
                Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(static_cast<T *>(ref.Get()));
            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }
    }

    /**
     * @brief Copy constructor for weak references.
     *
     * This constructor creates a new WeakRef that weakly references the same object
     * as the provided source WeakRef. If the source WeakRef is valid (points to a
     * control block), this constructor:
     * 1. Copies the control block pointer from the source
     * 2. Increments the weak reference count in that control block
     *
     * @param other The source WeakRef to copy from
     * @note - This maintains proper reference counting without affecting the
     *       lifetime of the referenced object
     */
    template <typename T>
    WeakRef<T>::WeakRef(const WeakRef &other) noexcept : m_ControlBlock(other.m_ControlBlock)
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->IncWeakCount();
        }
    }

    /**
     * @brief Copy conversion constructor for WeakRef objects of different but compatible types.
     *
     * This constructor allows creation of a WeakRef<T> from a WeakRef<U> where U is convertible to T
     * (typically through inheritance relationships). It properly maintains the weak reference counting
     * through the control block system.
     *
     * The implementation:
     * 1. Checks if the source WeakRef has a valid control block
     * 2. Retrieves or creates a control block for T* through the registry using a static_cast
     * 3. Increments the weak reference count if a valid control block is found
     *
     * @tparam U Source type that is convertible to T
     * @param other The source WeakRef<U> to convert from
     * @note - This constructor only participates in overload resolution if U* is convertible to T*
     */
    template <typename T>
    template <typename U, typename>
    WeakRef<T>::WeakRef(const WeakRef<U> &other) noexcept
    {
        if (other.m_ControlBlock)
        {
            m_ControlBlock = Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(
                static_cast<T *>(other.m_ControlBlock->GetPtr()));
            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }
    }

    /**
     * @brief Move constructor for weak references.
     *
     * This constructor creates a new WeakRef by transferring ownership of the control block
     * from the source WeakRef. After the move, the source WeakRef no longer references
     * any object (its control block pointer is set to nullptr).
     *
     * The implementation:
     * 1. Takes ownership of the control block pointer from the source WeakRef
     * 2. Sets the source WeakRef's control block pointer to nullptr to prevent
     *    both instances from managing the same control block
     *
     * Unlike the copy constructor, this constructor doesn't increment the weak reference count
     * since ownership is being transferred rather than shared.
     *
     * @param other The source WeakRef to move from
     */
    template <typename T>
    WeakRef<T>::WeakRef(WeakRef &&other) noexcept : m_ControlBlock(other.m_ControlBlock)
    {
        other.m_ControlBlock = nullptr;
    }

    /**
     * @brief Move conversion constructor for weak references of different but compatible types.
     *
     * This constructor moves a WeakRef<U> to a WeakRef<T> where U is convertible to T
     * (typically through inheritance relationships). Unlike the regular move constructor,
     * this constructor performs a type conversion which requires finding or creating a
     * control block for the target type.
     *
     * The implementation:
     * 1. Checks if the source WeakRef has a valid control block
     * 2. If valid, retrieves or creates a control block for the target type through the registry
     * 3. Sets the source WeakRef's control block to nullptr to transfer ownership
     * 4. No increment of weak reference count is needed as ownership is transferred
     *
     * @tparam U Source type that is convertible to T
     * @param other The source WeakRef<U> to move from
     * @note - This constructor only participates in overload resolution if U* is convertible to T*
     *       (enforced by the SFINAE template parameter)
     */
    template <typename T>
    template <typename U, typename>
    WeakRef<T>::WeakRef(WeakRef<U> &&other) noexcept
    {
        if (other.m_ControlBlock)
        {
            m_ControlBlock = Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(
                static_cast<T *>(other.m_ControlBlock->GetPtr()));
            other.m_ControlBlock = nullptr;
        }
    }

    /**
     * @brief Destructor for the weak reference.
     *
     * This destructor properly cleans up resources associated with the weak reference.
     * When a WeakRef is destroyed, it decrements the weak reference count in the associated
     * control block. If this was the last weak reference and the object has already been
     * destroyed (control block's pointer is null), the control block itself will be deleted.
     *
     * The destruction process ensures that:
     * 1. All weak references are properly tracked
     * 2. Control blocks are cleaned up when no longer needed
     * 3. No memory leaks occur when weak references go out of scope
     */
    template <typename T>
    WeakRef<T>::~WeakRef()
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
        }
    }

    /**
     * @brief Copy assignment operator for weak references.
     *
     * This operator assigns the content of another WeakRef to this WeakRef.
     * If this WeakRef is already referencing an object, it decrements the
     * weak reference count in that object's control block. Then it copies
     * the control block pointer from the source WeakRef and increments the
     * weak reference count if the control block is valid.
     *
     * The implementation:
     * 1. Checks for self-assignment to avoid unnecessary operations
     * 2. Decrements the weak reference count in the current control block (if any)
     * 3. Copies the control block pointer from the source WeakRef
     * 4. Increments the weak reference count in the new control block (if valid)
     *
     * @param other The source WeakRef to copy from
     * @return A reference to this WeakRef after the assignment
     * @note - This operator maintains proper reference counting without affecting the
     *       lifetime of the referenced object
     */
    template <typename T>
    WeakRef<T> &WeakRef<T>::operator=(const WeakRef &other) noexcept
    {
        if (this != &other)
        {
            if (m_ControlBlock)
            {
                m_ControlBlock->DecWeakCount();
            }

            m_ControlBlock = other.m_ControlBlock;

            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }
        return *this;
    }

    /**
     * @brief Copy conversion assignment operator for WeakRef objects of different but compatible types.
     *
     * This operator allows assignment of a WeakRef<U> to a WeakRef<T> where U is convertible to T
     * (typically through inheritance relationships). It properly maintains the weak reference counting
     * through the control block system.
     *
     * The implementation:
     * 1. Decrements the weak reference count for this WeakRef's current control block (if any)
     * 2. Retrieves or creates a control block for the T* pointer obtained by static_casting the U* pointer
     *    from the source WeakRef's control block
     * 3. Increments the weak reference count if a valid control block is found
     *
     * @tparam U Source type that is convertible to T
     * @param other The source WeakRef<U> to assign from
     * @return A reference to this WeakRef after the assignment
     * @note - This operator only participates in overload resolution if U* is convertible to T*
     *       (enforced by the SFINAE template parameter)
     */
    template <typename T>
    template <typename U, typename>
    WeakRef<T> &WeakRef<T>::operator=(const WeakRef<U> &other) noexcept
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
            m_ControlBlock = nullptr;
        }

        if (other.m_ControlBlock)
        {
            m_ControlBlock = Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(
                static_cast<T *>(other.m_ControlBlock->GetPtr()));
            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }

        return *this;
    }

    /**
     * @brief Move assignment operator for weak references.
     *
     * This operator assigns the content of another WeakRef to this WeakRef through move semantics.
     * Move assignment is more efficient than copy assignment as it transfers ownership of the
     * control block pointer rather than copying it and incrementing reference counts.
     *
     * The implementation:
     * 1. Checks for self-assignment to avoid unnecessary operations
     * 2. Decrements the weak reference count in the current control block (if any)
     * 3. Takes ownership of the control block pointer from the source WeakRef
     * 4. Sets the source WeakRef's control block pointer to nullptr to prevent
     *    both instances from managing the same control block
     *
     * @param other The source WeakRef to move from
     * @return A reference to this WeakRef after the assignment
     */
    template <typename T>
    WeakRef<T> &WeakRef<T>::operator=(WeakRef &&other) noexcept
    {
        if (this != &other)
        {
            if (m_ControlBlock)
            {
                m_ControlBlock->DecWeakCount();
            }

            m_ControlBlock = other.m_ControlBlock;
            other.m_ControlBlock = nullptr;
        }
        return *this;
    }

    /**
     * @brief Move conversion assignment operator for weak references of different but compatible types.
     *
     * This operator moves a WeakRef<U> to a WeakRef<T> where U is convertible to T
     * (typically through inheritance relationships). Unlike the regular move assignment operator,
     * this operator performs a type conversion which requires finding or creating a
     * control block for the target type.
     *
     * The implementation:
     * 1. Decrements the weak reference count of the current control block (if any)
     * 2. If the source WeakRef has a valid control block, retrieves or creates a control block
     *    for the target type through the registry
     * 3. Sets the source WeakRef's control block to nullptr to prevent both instances
     *    from managing the same control block
     * 4. No increment of weak reference count is needed as ownership is transferred
     *
     * @tparam U Source type that is convertible to T
     * @param other The source WeakRef<U> to move from
     * @return A reference to this WeakRef after the assignment
     * @note - This operator only participates in overload resolution if U* is convertible to T*
     *       (enforced by the SFINAE template parameter)
     */
    template <typename T>
    template <typename U, typename>
    WeakRef<T> &WeakRef<T>::operator=(WeakRef<U> &&other) noexcept
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
            m_ControlBlock = nullptr;
        }

        if (other.m_ControlBlock)
        {
            m_ControlBlock = Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(
                static_cast<T *>(other.m_ControlBlock->GetPtr()));
            other.m_ControlBlock = nullptr;
        }

        return *this;
    }

    /**
     * @brief Assignment operator that assigns a strong reference to a weak reference.
     *
     * This operator assigns a strong reference (Ref<U>) to a weak reference (WeakRef<T>).
     * It properly maintains weak reference counting through the control block system.
     *
     * The implementation:
     * 1. Decrements the weak reference count in the current control block (if any)
     * 2. Clears the current control block pointer
     * 3. If the source reference is valid, retrieves or creates a control block for the referenced object
     * 4. Increments the weak reference count if a valid control block is found
     *
     * @tparam U Source type that is convertible to T
     * @param ref The source Ref<U> to assign from
     * @return A reference to this WeakRef after the assignment
     * @note - This operator only participates in overload resolution if U* is convertible to T*
     *       (enforced by the SFINAE template parameter)
     */
    template <typename T>
    template <typename U, typename>
    WeakRef<T> &WeakRef<T>::operator=(const Ref<U> &ref) noexcept
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
            m_ControlBlock = nullptr;
        }

        if (ref)
        {
            m_ControlBlock =
                Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(static_cast<T *>(ref.Get()));
            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }

        return *this;
    }

    /**
     * @brief Assignment operator for assigning nullptr to a weak reference.
     *
     * This operator allows assigning nullptr to a WeakRef, which effectively
     * resets the weak reference. It decrements the weak reference count in the
     * associated control block (if any) and sets the control block pointer to nullptr.
     *
     * The implementation:
     * 1. Decrements the weak reference count if a valid control block exists
     * 2. Sets the control block pointer to nullptr
     *
     * @param unused Nullptr value (not used in the implementation)
     * @return A reference to this WeakRef after the assignment
     */
    template <typename T>
    WeakRef<T> &WeakRef<T>::operator=(std::nullptr_t) noexcept
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
            m_ControlBlock = nullptr;
        }

        return *this;
    }

    /**
     * @brief Constructor from std::weak_ptr.
     *
     * This constructor creates a WeakRef from a std::weak_ptr, enabling
     * interoperability with standard library smart pointers. The object
     * pointed to by the weak_ptr must inherit from RefCounted.
     *
     * The implementation:
     * 1. Attempts to lock the std::weak_ptr to get a temporary shared_ptr
     * 2. If successful, retrieves or creates a control block for the object
     * 3. Increments the weak reference count
     *
     * @param weak The std::weak_ptr to convert from
     * @note If the weak_ptr is expired, the WeakRef will be empty
     */
    template <typename T>
    WeakRef<T>::WeakRef(const std::weak_ptr<T>& weak) noexcept
    {
        if (auto shared = weak.lock())
        {
            m_ControlBlock = Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(shared.get());
            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }
    }

    /**
     * @brief Assignment operator from std::weak_ptr.
     *
     * This operator allows assigning a std::weak_ptr to a WeakRef, enabling
     * interoperability with standard library smart pointers.
     *
     * The implementation:
     * 1. Decrements the weak reference count of the current control block (if any)
     * 2. Attempts to lock the std::weak_ptr to get a temporary shared_ptr
     * 3. If successful, retrieves or creates a control block for the object
     * 4. Increments the weak reference count
     *
     * @param weak The std::weak_ptr to assign from
     * @return Reference to this WeakRef
     */
    template <typename T>
    WeakRef<T>& WeakRef<T>::operator=(const std::weak_ptr<T>& weak) noexcept
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
            m_ControlBlock = nullptr;
        }

        if (auto shared = weak.lock())
        {
            m_ControlBlock = Internal::ControlBlockRegistry<T>::GetInstance().GetControlBlock(shared.get());
            if (m_ControlBlock)
            {
                m_ControlBlock->IncWeakCount();
            }
        }

        return *this;
    }

    /**
     * @brief Checks if the object pointed to by the weak reference has been destroyed.
     *
     * This method determines whether the WeakRef is expired by checking if:
     * 1. The control block is null (indicating an empty weak reference), or
     * 2. The pointer stored in the control block is null (indicating the referenced object has been destroyed)
     *
     * A WeakRef becomes expired when the last Ref pointing to the same object is destroyed,
     * which triggers the object's deletion. The control block maintains this information
     * even after the object is gone.
     *
     * @return true if the referenced object has been destroyed or if this is an empty WeakRef
     * @return false if the referenced object is still alive
     */
    template <typename T>
    bool WeakRef<T>::Expired() const noexcept
    {
        return !m_ControlBlock || m_ControlBlock->GetPtr() == nullptr;
    }

    /**
     * @brief Attempts to convert a weak reference to a strong reference.
     *
     * This method tries to obtain a strong reference (Ref<T>) from the weak reference.
     * If the object of the WeakRef points to is still alive (not expired), it creates
     * and returns a new Ref<T> pointing to that object, which increments the reference
     * count of the object. If the object has been destroyed, it returns an empty Ref<T>.
     *
     * @tparam T The type of the referenced object
     * @return Ref<T> A strong reference to the object if it's still alive, or an empty reference otherwise
     */
    template <typename T>
    Ref<T> WeakRef<T>::Lock() const noexcept
    {
        if (!m_ControlBlock || m_ControlBlock->GetPtr() == nullptr) {
            return Ref<T>(nullptr);
        }

        T* ptr = static_cast<T*>(m_ControlBlock->GetPtr());
        return Ref<T>(ptr);
    }

    /**
     * @brief Resets this weak reference to empty state.
     *
     * This method explicitly releases the weak reference to any object it might be pointing to.
     * It decrements the weak reference count in the associated control block, and if this
     * was the last weak reference and the object has already been destroyed, the control block
     * itself will be deleted.
     *
     * After calling Reset(), the weak reference will be in an empty state (similar to a
     * default-constructed WeakRef) and will return true for Expired() and nullptr for Lock().
     *
     * @note - This method is often used to explicitly release resources before the WeakRef
     *       goes out of scope, or to prepare the WeakRef for reuse.
     */
    template <typename T>
    void WeakRef<T>::Reset() noexcept
    {
        if (m_ControlBlock)
        {
            m_ControlBlock->DecWeakCount();
            m_ControlBlock = nullptr;
        }
    }

    /**
     * @brief Gets the current number of strong references (Ref<T>) to the object.
     *
     * This method returns the reference count of the object that this WeakRef
     * points to. If the WeakRef is expired (the object has been destroyed) or
     * if it's an empty WeakRef, the method returns 0.
     *
     * This is useful for debugging and testing purposes, or for algorithms that
     * need to make decisions based on the reference count of an object.
     *
     * @tparam T The type of the referenced object
     * @return The number of strong references to the object, or 0 if the WeakRef is expired
     */
    template <typename T>
    uint32_t WeakRef<T>::UseCount() const noexcept
    {
        if (m_ControlBlock && m_ControlBlock->GetPtr())
        {
            return m_ControlBlock->GetPtr()->GetRefCount();
        }

        return 0;
    }

    /**
     * @brief Equality comparison operator for WeakRef objects.
     *
     * This operator determines if two WeakRef objects reference the same underlying object.
     * The comparison is done in the following order:
     * 1. First checks if both WeakRefs have the same control block pointer (fast path)
     * 2. If control blocks differ, checks if either is nullptr (meaning one reference is empty)
     * 3. Finally compares the actual object pointers stored in the control blocks
     *
     * This enables WeakRef objects to be used in containers that require equality comparison,
     * such as std::set, std::map, or for general comparison operations.
     *
     * @param other The WeakRef to compare with
     * @return true if both WeakRef objects reference the same object or are both empty
     * @return false if the WeakRef objects reference different objects or one is empty and one is not
     */
    template <typename T>
    bool operator==(const WeakRef<T> &lhs, const WeakRef<T> &rhs) noexcept
    {
        if (lhs.Expired() || rhs.Expired())
            return false;

        auto lhsLocked = lhs.Lock();
        auto rhsLocked = rhs.Lock();

        return lhsLocked == rhsLocked;
    }

    /**
     * @brief Inequality comparison operator for WeakRef objects.
     *
     * This operator determines if two WeakRef objects reference different underlying objects.
     * It is implemented by negating the result of the equality operator.
     *
     * This operator requires that type T has a valid operator== defined.
     *
     * @tparam T The type of objects managed by the WeakRef instances
     * @param lhs The left-hand side WeakRef for comparison
     * @param rhs The right-hand side WeakRef for comparison
     * @return True if the WeakRefs manage different objects, false if they manage equal objects
     */
    template <typename T>
    bool operator!=(const WeakRef<T> &lhs, const WeakRef<T> &rhs) noexcept
    {
        return !(lhs == rhs);
    }

    /**
     * @brief Explicit template instantiation for Ref<RefCounted>
     *
     * This explicit instantiation ensures that the compiler generates all the code
     * for Ref<RefCounted> at this point, making it available to all translation units
     * that include this header without having to recompile the template for each use.
     *
     * RefCounted is the base class for all reference-counted objects in the system,
     * so this instantiation is particularly important for the smart pointer system.
     */
    template class Ref<RefCounted>;

    /**
     * @brief Explicit template instantiation for WeakRef<RefCounted>
     *
     * This explicit instantiation ensures that the compiler generates all the code
     * for WeakRef<RefCounted> at this point, making it available to all translation units
     * that include this header without having to recompile the template for each use.
     *
     * Weak references to RefCounted objects allow tracking objects without preventing their
     * deletion when all strong references (Ref<T>) are gone, which is essential for
     * breaking reference cycles and implementing observer patterns.
     */
    template class WeakRef<RefCounted>;

}

