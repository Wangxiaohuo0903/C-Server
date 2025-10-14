// Simple API wrapper for the AI server

const API = {
    baseURL: 'http://localhost:8080',

    // User registration
    async register(username, password) {
        const formData = new URLSearchParams();
        formData.append('username', username);
        formData.append('password', password);

        const response = await fetch(`${this.baseURL}/register`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: formData.toString()
        });

        const text = await response.text();
        if (!response.ok) {
            throw new Error(text || 'Registration failed');
        }
        return text;
    },

    // User login
    async login(username, password) {
        const formData = new URLSearchParams();
        formData.append('username', username);
        formData.append('password', password);

        const response = await fetch(`${this.baseURL}/login`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: formData.toString()
        });

        const text = await response.text();
        if (!response.ok) {
            throw new Error(text || 'Login failed');
        }
        return text;
    },

    // Create new chat session
    async createChat(user) {
        const formData = new URLSearchParams();
        formData.append('user', user);

        const response = await fetch(`${this.baseURL}/chat`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: formData.toString()
        });

        const chatId = await response.text();
        if (!response.ok) {
            throw new Error('Failed to create chat');
        }
        return parseInt(chatId);
    },

    // Get chat messages
    async getChatMessages(chatId) {
        const response = await fetch(`${this.baseURL}/chat/${chatId}`);

        if (!response.ok) {
            throw new Error('Failed to fetch messages');
        }

        return await response.json();
    },

    // Send message and get AI response
    async sendMessage(chatId, user, prompt) {
        const response = await fetch(`${this.baseURL}/infer`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                chat_id: chatId.toString(),
                user: user,
                prompt: prompt
            })
        });

        if (!response.ok) {
            throw new Error('Failed to get AI response');
        }

        const data = await response.json();
        return data.answer;
    }
};

// Storage helper
const Storage = {
    setUser(username) {
        localStorage.setItem('username', username);
    },

    getUser() {
        return localStorage.getItem('username');
    },

    setChatId(chatId) {
        localStorage.setItem('chatId', chatId.toString());
    },

    getChatId() {
        const chatId = localStorage.getItem('chatId');
        return chatId ? parseInt(chatId) : null;
    },

    getChats() {
        const chats = localStorage.getItem('chats');
        return chats ? JSON.parse(chats) : [];
    },

    setChats(chats) {
        localStorage.setItem('chats', JSON.stringify(chats));
    },

    clear() {
        localStorage.removeItem('username');
        localStorage.removeItem('chatId');
        localStorage.removeItem('chats');
    }
};
