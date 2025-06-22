#pragma once

#include <vector>

#include "io/io.h"
#include "screen/components/component.h"

class Page
{
public:
    Page() = default;

    virtual ~Page() = default;

    // Method to render the page
    inline void render()
    {
        for (auto &component : components)
        {
            component->draw();
        }
    }

    // Method to handle input events
    virtual void handleInput(Event *e) = 0;

private:
    std::vector<Component *> components; // Components on the page

    // Prevent copying
    Page(const Page &) = delete;
    Page &operator=(const Page &) = delete;

protected:
    inline void addComponent(Component *component)
    {
        components.push_back(component);
    }
    inline void removeComponent(Component *component)
    {
        components.erase(std::remove(components.begin(), components.end(), component), components.end());
    }
    inline void clearComponents()
    {
        for (auto &component : components)
        {
            delete component; // Free memory
        }
        components.clear();
    }
};